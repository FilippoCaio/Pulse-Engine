// ─── Pulse Engine — native editor with docking (Win32 + OpenGL, zero deps) ───
#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS   // build.bat may already define it
#endif
#include "glext.h"
#include <windowsx.h>
#include <commdlg.h>
#include <shlobj.h>   // SHBrowseForFolder (project hub folder picker)
#include "math.h"
#include "physics.h"
#include "render.h"
#include "scene.h"
#include "ui.h"
#include "dock.h"
#include "blueprint.h"
#include "curve.h"
#include "material.h"
#include "widget.h"
#include "animation.h"
#include "audio.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdarg>
#include <string>
#include <vector>
#include <map>
#include <set>
#include <memory>
#include <unordered_set>
#include <filesystem>
#include <algorithm>
#include <cctype>
#include <queue>
#include <limits>
#include <sstream>
#include <iomanip>

namespace fs = std::filesystem;

static const float FIXED_DT = 1.0f / 60.0f;
static const float MENUBAR_H = 30;
static const float DOC_TAB_H = 36;      // larger document tabs (Level + blueprints)
static const float TOP_H = MENUBAR_H + DOC_TAB_H;
static const float BOTTOM_BAR_H = 30;   // Unreal-style status bar at the very bottom

enum class Mode { Edit, Play };

struct LogLine { int level=0; std::string text; Vec3 color{}; float alpha=1; bool customColor=false; };

// per-view content-browser navigation (the dock panel and each Ctrl+Space drawer
// keep their own); the folder tree and clipboard are shared globally
struct BrowserState {
    std::string curRel;
    std::vector<std::string> curDirs, curPfbs, curImps, curBps, curBpis, curCurves, curAnimations, curAnimators;
    std::vector<std::string> curMeshes, curTextures, curAudio;
    std::vector<std::string> curAudioClasses, curAudioAttenuations, curAudioConcurrencies;
    std::vector<std::string> curEnums;
    std::vector<std::string> curMaterials;
    std::vector<std::string> curWidgets;
    std::string browserSel;
    std::set<std::string> browserSelected;
    std::string browserSelectionAnchor;
    float browserSplit = 190;
    float browserTileHeight = 84;
    bool browserSplitDrag = false;
    char pathEdit[260] = "";
    std::string pathEditSynced;
    int browserCtx = 0;
    std::string ctxName;
    std::string ctxRelPath;          // full relative path for folder-tree menus
    int ctxIcon = 0;
    float ctxX = 0, ctxY = 0;
    bool ctxMoveOpen = false;
    bool ctxCreateOpen = false;
    bool renameOpen = false;
    std::string renameRel;
    char renameBuf[128] = "";
    bool renameAutoFocus = false;
    float ctxMoveScroll = 0;
    std::string dragItem;
    int dragItemIcon = -1;
    bool dragItemActive = false;
    float dragItemX = 0, dragItemY = 0;
    std::string dropHoverRel;
    bool dropHoverValid = false;
    std::string dropTileName;
};

// a summonable content browser (Unreal-style content drawer): slides up from the
// bottom, then can be undocked into a free-floating window
struct ContentDrawer {
    BrowserState st;
    float anim = 0;          // 0..1 slide progress
    bool closing = false;
    bool floating = false;   // bottom drawer vs. free-floating draggable window
    UIRect rect = { 260, 140, 680, 340 };
    bool dragging = false, resizing = false;
    bool bottomResizing = false;
    int resizeEdges = 0;
    float dragOX = 0, dragOY = 0;
    float bottomHeight = 0;
};

// panel detached into its own OS window (drag it to another monitor)
struct NativeWin {
    std::string dockId;
    HWND hwnd = nullptr;
    HDC hdc = nullptr;
    int width = 640, height = 480;
    UI ui;
    UIInput in;
    bool wantClose = false;
};

// a project known to the hub (launcher). name = folder name; lastLevel is relative
struct HubProject {
    std::string path;        // absolute project folder
    std::string name;        // display name (folder stem)
    std::string lastLevel;   // last edited level, relative to path ("" = none)
};

struct BuildSceneEntry {
    std::string rel;
    bool include = true;
};

// A live UI widget during Play: its own copy of the tree (text/value are mutated
// at runtime) plus a running instance of the graph stored in the same .wgt.
struct RuntimeWidget {
    int handle = 0;
    std::string assetPath;
    WidgetAsset asset;
    bool visible = false;      // Add/Remove from Viewport
    BPInstance inst;           // graph lives in App::widgetGraphCache (stable address)
    bool constructed = false;  // Event Construct already fired for this viewport add
    std::string hoverElement;  // element under the pointer, for Enter/Exit
    std::string pressElement;  // element the press started on
};

struct App {
    HWND hwnd = nullptr;
    HDC hdc = nullptr;
    HGLRC glrc = nullptr;
    int pixelFormat = 0;
    PIXELFORMATDESCRIPTOR pfd = {};
    std::vector<std::unique_ptr<NativeWin>> natives;
    int width = 1720, height = 960;
    bool running = true;

    EditorScene scene;
    struct NavigationData {
        Vec3 origin;
        float cellSize = 0.5f;
        float stepHeight = 0.6f;
        float agentRadius = 0.45f;
        int width = 0, height = 0;
        std::vector<float> cells; // NaN = not walkable; otherwise surface Y
        bool baked = false;
        bool show = true;
        int sourceCount = 0;
        int occluderCount = 0;
        std::string status = "NavMesh non generata.";
    } navigation;
    bool navigationShowBeforePlay = true;
    AudioSystem audio;
    struct AudioFade {
        float multiplier = 1.0f;
        float start = 1.0f;
        float target = 1.0f;
        float duration = 0.0f;
        float elapsed = 0.0f;
        bool stopWhenDone = false;
    };
    std::map<int, AudioFade> audioFades;
    std::map<std::string, AudioClassAsset> audioClassCache;
    std::map<std::string, AudioAttenuationAsset> audioAttenuationCache;
    std::map<std::string, AudioConcurrencyAsset> audioConcurrencyCache;
    std::map<std::string, std::vector<int>> audioConcurrencyOwners;
    int audioAssetEditKind = -1;             // 0 class, 1 attenuation, 2 concurrency
    std::string audioAssetEditRel;
    AudioClassAsset audioClassEdit;
    AudioAttenuationAsset audioAttenuationEdit;
    AudioConcurrencyAsset audioConcurrencyEdit;
    std::string enumAssetEditRel;
    BPEnumAsset enumAssetEdit;
    std::string animationEditRel;
    AnimationClipAsset animationEdit;
    float animationTime = 0;
    bool animationPlaying = false;
    bool animationRecording = false;
    bool animationFocused = false;
    int animationSelectedKey = -1;
    std::vector<int> animationSelectedKeys;
    int animationSelectedEvent = -1;
    int animationDraggingEvent = -1;
    float animationEventDragOffset = 0;
    char animationEventName[64] = "";
    std::vector<AnimationKey> animationKeyClipboard;
    float animationClipboardOrigin = 0;
    struct AnimationPreviewTransform { int entityId = 0; Vec3 position; Quat rotation; Vec3 scale; };
    std::vector<AnimationPreviewTransform> animationPreviewOriginal;
    bool animationPreviewActive = false;
    bool animationPanelDrawnThisFrame = false;
    bool animationPanelWasDrawn = false;
    float animationTimelineStart = 0.0f;
    float animationTimelinePixelsPerSecond = 100.0f;
    bool animationTimelinePanning = false;
    float animationTimelineLastMouseX = 0;
    int animationDraggingKey = -1;
    float animationKeyDragOffset = 0;
    float animationKeyDragMouseTime = 0;
    std::vector<float> animationKeyDragStartTimes;
    bool animationMarqueeSelecting = false;
    float animationMarqueeStartX = 0, animationMarqueeStartY = 0;
    std::vector<int> animationMarqueeBaseSelection;
    int animationObservedEntity = 0;
    Vec3 animationObservedPos, animationObservedScale;
    Quat animationObservedRot;
    std::string animatorEditRel;
    AnimatorControllerAsset animatorEdit;
    std::map<std::string, AnimatorControllerAsset> runtimeAnimatorControllers;
    std::map<std::string, AnimationClipAsset> runtimeAnimationClips;
    struct AnimatorBaseTransform {
        // Stored in parent-local space when the animated object is parented.
        Vec3 position; Quat rotation; Vec3 scale;
        int parentId = 0;
        Quat referenceParentRotation;
        Vec3 referenceParentScale{1,1,1};
    };
    std::map<unsigned long long, AnimatorBaseTransform> runtimeAnimatorBases;
    struct AnimationTriggerBinding {
        int animatorId = 0;
        int listenerId = 0;
        std::string trigger;
        std::string customEvent;
    };
    std::vector<AnimationTriggerBinding> animationTriggerBindings;
    std::vector<std::pair<int,std::string>> pendingAnimationTriggers;
    int animatorSelectedState = 0;
    int animatorSelectedTransition = -1;
    int animatorSection = 0;       // 0 state machine, 1 parameters
    int animatorSelectedParameter = -1;
    int animatorConnectFrom = 0;
    int animatorDragState = 0;
    float animatorDragOffX = 0, animatorDragOffY = 0;
    float animatorPanX = 24, animatorPanY = 24, animatorZoom = 1.0f;
    bool animatorPanning = false;
    float animatorLastMouseX = 0, animatorLastMouseY = 0;
    float animatorVariablesWidth = 210.0f;
    float animatorInspectorWidth = 330.0f;
    bool animatorVariablesResizing = false;
    bool animatorInspectorResizing = false;
    int animatorNameState = 0;
    char animatorStateName[64] = "";
    char animatorParameterName[64] = "";
    float frameDt = 1.0f / 60.0f;
    Renderer renderer;
    OrbitCamera camera;
    UI ui;
    UIInput uiIn;
    DockManager dock;
    Frame* frameForRender = nullptr;   // current frame, for viewport 3D repaints (dock overlay / native windows)

    Mode mode = Mode::Edit;
    bool paused = false;
    std::string snapshot;
    int selectedId = 0;
    std::set<int> selectedIds;
    int outlinerSelectionAnchor = 0;
    std::vector<int> outlinerVisibleOrder;
    std::vector<std::string> sceneUndoHistory;
    std::vector<std::string> sceneRedoHistory;
    bool sceneHistoryGesture = false;
    bool sceneHistorySkipFrame = false;
    std::string sceneHistoryGestureBefore;
    // viewport toolbar / debug visibility
    int gizmoMode = 0;                 // 0 sposta, 1 ruota, 2 scala
    bool gizmoLocal = false;            // false = assi world, true = assi locali dell'oggetto
    bool viewportFocused = false;
    bool outlinerFocused = false;
    bool browserDeletePending = false;  // Delete pressed over the Content browser/drawer
    std::string pendingOpenLevel;       // Open Level (BP): level to load after the sim step
    std::string currentLevelName;       // stem of the running level (Get Current Level Name)
    bool showGizmo = true;
    bool showGrid = true;
    bool showColliders = true;
    bool showContacts = false;         // contact points: off by default (debug)
    bool transformSnap = false;
    float moveSnap = 10.0f, rotateSnap = 15.0f, scaleSnap = 0.1f;
    bool vsync = true;
    bool logScrollPending = false;
    float audioGainUpdateTimer = 0.0f;
    std::vector<LogLine> logs;
    char projectPath[MAX_PATH] = "";
    std::string baseDir, projectDir;
    std::string projectName = "progetto";    // display name of the open project's root
    std::string gameInstanceAsset;             // project-wide GameInstance class
    std::string defaultGameModeAsset;          // project-wide default GameMode (levels without one)
    std::string startupLevel;                  // project-wide scene loaded when the project opens
    std::string language = "English";          // UI language preference (only English for now)
    // Project Settings modal (File > Impostazioni progetto)
    bool settingsWindowOpen = false;
    int settingsCategory = 0;                  // 0 Generali, 1 Fisica, 2 Rendering, 3 Lingua

    // Windows build settings. Scene order is also the runtime order; the first
    // included scene is the startup level of the packaged prototype.
    bool buildWindowOpen = false;
    bool buildScenesScanned = false;
    std::vector<BuildSceneEntry> buildScenes;
    int buildSceneSelected = -1;
    char buildOutput[MAX_PATH] = "";
    std::string buildStatus;

    // Fullscreen is borderless-monitor fullscreen, so switching in and out does
    // not recreate the OpenGL context. Play fullscreen hides editor chrome but
    // keeps the compact Pause/Stop bar visible.
    bool windowFullscreen = false;
    bool editorFullscreen = false;
    bool playFullscreenOption = false;
    bool playFullscreenActive = false;
    DWORD windowedStyle = WS_OVERLAPPEDWINDOW;
    WINDOWPLACEMENT windowedPlacement = { sizeof(WINDOWPLACEMENT) };

    // A packaged executable discovers impulso_build.cfg next to itself.
    bool standaloneMode = false;
    std::vector<std::string> standaloneScenes;

    // ── project hub (launcher shown at startup) ──
    bool inHub = true;                        // showing the hub instead of the editor
    std::vector<HubProject> hubProjects;
    float hubScroll = 0;
    int hubHover = -1;

    // top-level document tabs: 0 = Level, then blueprints/interfaces, curves, materials
    std::vector<std::unique_ptr<BPEditor>> bpDocs;
    std::vector<std::unique_ptr<CurveEditor>> curveDocs;
    std::vector<std::unique_ptr<MaterialEditor>> materialDocs;
    std::vector<std::unique_ptr<WidgetEditor>> widgetDocs;
    std::map<std::string, MaterialAsset> materialCache;   // parsed .mat by rel path (for scene rendering)
    // unique_ptr so a widget graph can create another widget while it runs
    // without the vector reallocating under the instance that is executing
    std::vector<std::unique_ptr<RuntimeWidget>> runtimeWidgets;
    std::map<std::string, BPGraph> widgetGraphCache;      // .wgt path → graph (BPInstance points here)
    int nextWidgetHandle = 1;
    int hudWidgetHandle = 0;                              // the scene's HUD widget, as a runtime widget
    int activeDoc = 0;
    int closeDocRequest = -1;         // deferred: blueprint tab to close
    int closeCurveDocRequest = -1;
    int closeMaterialDocRequest = -1;
    int closeWidgetDocRequest = -1;
    float docTabX = 0;                // running x while laying out the doc tab bar

    // Play mouse capture (FPS-style: cursor locked to the viewport)
    bool playMouseCaptured = false;

    // Ctrl+Space content drawers (each keeps its own navigation)
    std::vector<ContentDrawer> drawers;
    bool openDrawerRequest = false;
    BrowserState lastDrawerBrowser;
    bool hasLastDrawerBrowser = false;

    bool prefabEditMode = false;
    std::string prefabEditRel;
    std::string prefabEditSceneBackup;
    int prefabEditPreviousSelection = 0;
    std::string prefabEditLevelPath;

    // content browser
    struct FolderNode { std::string rel, name; std::vector<int> kids; };
    std::vector<FolderNode> folders;          // [0] = project root
    std::unordered_set<std::string> folderCollapsed;
    std::map<std::string, Vec3> folderColors;
    std::map<std::string, GLuint> assetIconTextures;
    std::string curRel;                       // current folder relative to projectDir ("" = root)
    std::vector<std::string> curDirs, curPfbs, curImps, curBps, curBpis, curCurves, curAnimations, curAnimators;
    std::vector<std::string> curMeshes, curTextures, curAudio;
    std::vector<std::string> projectMeshAssets;
    std::vector<std::string> projectMaterialAssets;
    std::vector<std::string> projectWidgetAssets;
    std::vector<std::string> curAudioClasses, curAudioAttenuations, curAudioConcurrencies;
    std::vector<std::string> curEnums;
    std::vector<std::string> curMaterials;
    std::vector<std::string> curWidgets;
    char newFolderName[40] = "";
    std::string browserSel;                   // selected tile name
    std::set<std::string> browserSelected;    // selected content paths (relative to project)
    std::string browserSelectionAnchor;       // anchor path for Shift-range selection
    std::vector<std::string> browserVisibleOrder;

    // content browser v2
    float browserSplit = 190;                 // width of the folder column
    float browserTileHeight = 84;             // Ctrl+wheel icon zoom per browser view
    float browserTileHeightPreference = 84;   // persisted default for future drawers/sessions
    bool browserSplitDrag = false;
    char pathEdit[260] = "";
    std::string pathEditSynced;
    int browserCtx = 0;                       // 0 closed, 1 on item, 2 on empty area
    std::string ctxName;                      // item name for browserCtx == 1
    std::string ctxRelPath;                   // folder-tree target for browserCtx == 3
    int ctxIcon = 0;
    float ctxX = 0, ctxY = 0;
    bool ctxMoveOpen = false;                 // "Move to..." submenu
    bool ctxCreateOpen = false;               // "Create asset" submenu
    bool renameOpen = false;
    std::string renameRel;
    char renameBuf[128] = "";
    bool renameAutoFocus = false;
    float ctxMoveScroll = 0;
    std::string fileClipboard;                // abs path of copied file/folder
    std::string dragItem;                     // browser drag & drop source name
    int dragItemIcon = -1;
    bool dragItemActive = false;
    float dragItemX = 0, dragItemY = 0;
    std::string dropHoverRel;                 // folder-tree row hovered during a drag
    bool dropHoverValid = false;              // dropHoverRel is hovered THIS frame
    std::string dropTileName;                 // folder tile hovered during a drag

    std::string clipboard;                    // serialized subtree for Ctrl+C/V
    bool addCompOpen = false;                 // inspector "add component" accordion
    bool staticMenuOpen = false;
    int staticMenuEntity = 0;
    int detailsDragEntity = 0;                // inspector component-card reorder
    int detailsDragComponent = -1;
    // Unity-style smooth drag: the grabbed card follows the cursor inside the
    // Details panel and the drop snaps to the gap between two cards.
    int detailsDragBpIndex = -1;              // blueprint component being dragged (-1 = not a BP)
    float detailsDragGrabDY = 0;              // cursor→card-top offset at grab time
    float detailsDragCardH = 0;               // grabbed card height (ghost size)
    std::string detailsDragTitle;
    // per-frame layout of the top-level cards, filled while the Details panel draws
    struct DetailCardBox { int kind; int bpIndex; float y, h; };
    std::vector<DetailCardBox> detailCards;
    std::string assetFieldOpen;               // id of the open big asset-picker dropdown
    int componentResetMenuEntity = 0;
    int componentResetMenuKind = -1;
    float componentResetMenuX = 0, componentResetMenuY = 0;
    std::map<std::string, BPGraph> bpEditCache; // edit-time graph cache (exposed vars in Details)
    int jointTargetPick = 0;                  // add-component joint target index
    int outlinerContextEntity = 0;
    float outlinerContextX = 0, outlinerContextY = 0;
    bool outlinerComponentSubmenu = false;
    float outlinerComponentScroll = 0;

    // blueprint runtime (play mode)
    struct LiveScript { int entityId = 0; int componentIndex = 0; BPInstance inst; };
    std::vector<LiveScript> bpScripts;
    std::vector<int> bpSpawnScriptQueue;
    std::map<int, std::map<std::string, BPValue>> bpSpawnOverrides;
    bool bpProcessingSpawns = false;
    bool bpWorldBegun = false;
    std::map<std::string, BPGraph> graphCache;
    std::map<std::string, CurveAsset> curveCache;
    bool bpKeysDown[64] = {};
    std::vector<int> bpKeyEvents;             // fresh presses  → Started
    std::vector<int> bpKeyReleases;           // releases       → Completed
    float bpMouseDX = 0, bpMouseDY = 0;       // mouse delta accumulated this frame (Play)
    float bpWheelAccum = 0;
    float bpAxisValues[8] = {};               // per-frame values for InputAxis nodes
    bool bpBindAxisActive[3] = {};            // InputAction bound to Mouse X/Y/XY: moving?
    std::vector<int> bpDestroyQueue;
    double playTime = 0;
    int gameModeEntity = 0;
    int gameInstanceEntity = 0;
    int playerControllerEntity = 0;
    int playerPawnEntity = 0;
    std::map<std::string, BPVarStore> persistentGameInstanceVars;

    // trace debug segments (Unreal-style), each with a short lifetime
    struct DebugSeg { Vec3 a, b, color; float life; };
    std::vector<DebugSeg> debugSegs;

    // free-fly camera (Play with no game camera, or paused RMB "eject")
    bool flyActive = false, flyLook = false;
    Vec3 flyPos;
    float flyYaw = 0, flyPitch = 0;

    float sunAzimuth = 40, sunElevation = 42, sunIntensity = 1.12f;
    float fogDensity = 0.006f, shadowStrength = 0.85f;

    bool orbiting = false, panning = false;
    int mouseX = 0, mouseY = 0;
    int gizmoAxis = -1, hoverAxis = -1;
    Vec3 gizmoStartPos;
    float gizmoStartT = 0;
    Quat gizmoStartQuat;
    Vec3 gizmoStartScale{ 1, 1, 1 };
    Vec3 gizmoDragAxis{ 1, 0, 0 };
    Vec3 gizmoRotationStartVector{ 1, 0, 0 };
    float gizmoRotationDeltaDeg = 0;
    float gizmoStartAIBaseOffset = 0.5f;

    // outliner tree state
    std::unordered_set<int> collapsed;
    int treeDragId = 0;
    int treeDropId = 0;
    float treePressX = 0, treePressY = 0;
    bool treeDragging = false;

    // stable Euler state for the inspector rotation fields (avoids gimbal re-extraction)
    Vec3 inspEuler;
    int inspEulerId = 0;

    RigidBody* grabBody = nullptr;
    Vec3 grabLocal;
    float grabDist = 0;
    Vec3 grabTarget;

    bool spaceQueued = false;
    int shots = 0;
    double accumulator = 0;
    float fps = 60;
};

static App g;
static void addLog(int level, const char* fmt, ...);
static void restoreAnimationPreview();
static void applyAnimationPreview(float time);
static void animationClearKeySelection();

static void clearSceneHistory() {
    g.sceneUndoHistory.clear();
    g.sceneRedoHistory.clear();
    g.sceneHistoryGesture = false;
    g.sceneHistoryGestureBefore.clear();
    g.sceneHistorySkipFrame = true;
}

static void pushSceneUndo(const std::string& state) {
    if (!g.sceneUndoHistory.empty() && g.sceneUndoHistory.back() == state) return;
    g.sceneUndoHistory.push_back(state);
    if (g.sceneUndoHistory.size() > 50) g.sceneUndoHistory.erase(g.sceneUndoHistory.begin());
    g.sceneRedoHistory.clear();
}

static void finishSceneHistoryFrame(const std::string& before) {
    if (before.empty() || g.mode != Mode::Edit || g.inHub) return;
    if (g.sceneHistorySkipFrame) {
        g.sceneHistorySkipFrame = false;
        g.sceneHistoryGesture = false;
        g.sceneHistoryGestureBefore.clear();
        return;
    }
    const std::string after = g.scene.serialize();
    if (after != before) {
        if (g.uiIn.mouseDown) {
            if (!g.sceneHistoryGesture) {
                g.sceneHistoryGesture = true;
                g.sceneHistoryGestureBefore = before;
            }
        } else {
            pushSceneUndo(before);
        }
    }
    if (g.sceneHistoryGesture && !g.uiIn.mouseDown) {
        if (after != g.sceneHistoryGestureBefore) pushSceneUndo(g.sceneHistoryGestureBefore);
        g.sceneHistoryGesture = false;
        g.sceneHistoryGestureBefore.clear();
    }
}

static void undoScene() {
    if (g.mode != Mode::Edit || g.sceneUndoHistory.empty()) return;
    std::string current = g.scene.serialize();
    std::string previous = g.sceneUndoHistory.back();
    g.sceneUndoHistory.pop_back();
    if (!g.scene.deserialize(previous)) return;
    g.audio.stopAll(); g.audioFades.clear();
    g.navigation.baked = false; g.navigation.cells.clear();
    g.navigation.status = "Scene changed: run the Navigation Bake again.";
    g.sceneRedoHistory.push_back(std::move(current));
    if (g.sceneRedoHistory.size() > 50) g.sceneRedoHistory.erase(g.sceneRedoHistory.begin());
    if (!g.scene.byId(g.selectedId)) g.selectedId = 0;
    g.sceneHistorySkipFrame = true;
    addLog(0, "Undo scene change (Ctrl+Z).");
}

static void redoScene() {
    if (g.mode != Mode::Edit || g.sceneRedoHistory.empty()) return;
    std::string current = g.scene.serialize();
    std::string next = g.sceneRedoHistory.back();
    g.sceneRedoHistory.pop_back();
    if (!g.scene.deserialize(next)) return;
    g.audio.stopAll(); g.audioFades.clear();
    g.navigation.baked = false; g.navigation.cells.clear();
    g.navigation.status = "Scene changed: run the Navigation Bake again.";
    g.sceneUndoHistory.push_back(std::move(current));
    if (g.sceneUndoHistory.size() > 50) g.sceneUndoHistory.erase(g.sceneUndoHistory.begin());
    if (!g.scene.byId(g.selectedId)) g.selectedId = 0;
    g.sceneHistorySkipFrame = true;
    addLog(0, "Redo scene change (Ctrl+X).");
}

// ═══ log ═══
static void addLog(int level, const char* fmt, ...) {
    char buf[512];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    LogLine line;line.level=level;line.text=buf;g.logs.push_back(line);
    if (g.logs.size() > 400) g.logs.erase(g.logs.begin(), g.logs.begin() + 100);
    g.logScrollPending = true;
}

static void bpPrintStringCb(Entity* entity,const char* text,const Vec3& color,float alpha){
    LogLine line;line.level=1;line.text="[BP "+std::string(entity?entity->name:"?")+"] "+(text?text:"");
    line.color=color;line.alpha=clampf(alpha,0,1);line.customColor=true;g.logs.push_back(std::move(line));
    if(g.logs.size()>400)g.logs.erase(g.logs.begin(),g.logs.begin()+100);g.logScrollPending=true;
}

static bool readFile(const std::string& path, std::string& out);
static bool writeFile(const std::string& path, const std::string& data);
static void setWindowFullscreen(bool enabled);
static bool loadStandaloneManifest();
static bool playAudioSource(Entity& e, bool resetFade = true);
static void fadeInAudioSource(Entity& e, float duration);
static void fadeOutAudioSource(Entity& e, float duration);
static void setAudioSourceVolume(Entity& e, float volume);
static void setAudioSourceClip(Entity& e, const char* clip);
static bool savePrefabEdit();
static void closePrefabEdit(bool saveChanges);
static bool appBlueprintPathIsA(std::string actual, std::string requested);

// ═══ project folder ═══
static void initDirs() {
    char exePath[MAX_PATH];
    GetModuleFileNameA(nullptr, exePath, MAX_PATH);
    fs::path p = fs::path(exePath).parent_path();
    if (p.filename() == "build") p = p.parent_path();
    g.baseDir = p.string();
    g.projectDir = (p / "progetto").string();
    gBPProjectDir = g.projectDir;
    std::error_code ec;
    fs::create_directories(g.projectDir, ec);
}

static void loadEditorPreferences() {
    std::string data;
    if (!readFile(g.baseDir + "\\editor.cfg", data)) return;
    float tileHeight = 84.0f;
    if (sscanf(data.c_str(), "browser_tile_height %f", &tileHeight) == 1) {
        g.browserTileHeightPreference = clampf(tileHeight, 64.0f, 144.0f);
        g.browserTileHeight = g.browserTileHeightPreference;
    }
    std::istringstream lines(data); std::string line;
    while(std::getline(lines,line)){
        if(line.rfind("folder_color ",0)==0){std::istringstream s(line.substr(13));std::string rel;Vec3 c;if(s>>std::quoted(rel)>>c.x>>c.y>>c.z)g.folderColors[rel]=c;}
        else if(line.rfind("navigation_visible ",0)==0){int visible=1;if(sscanf(line.c_str(),"navigation_visible %d",&visible)==1)g.navigation.show=visible!=0;}
        else if(line.rfind("frustum_culling ",0)==0){int on=1;if(sscanf(line.c_str(),"frustum_culling %d",&on)==1)g.renderer.frustumCulling=on!=0;}
    }
}

static void saveEditorPreferences() {
    std::ostringstream data;data<<"browser_tile_height "<<g.browserTileHeightPreference<<"\n";
    data<<"navigation_visible "<<(g.navigation.show?1:0)<<"\n";
    data<<"frustum_culling "<<(g.renderer.frustumCulling?1:0)<<"\n";
    for(const auto& it:g.folderColors)data<<"folder_color "<<std::quoted(it.first)<<" "<<it.second.x<<" "<<it.second.y<<" "<<it.second.z<<"\n";
    writeFile(g.baseDir + "\\editor.cfg", data.str());
}

static void setWindowFullscreen(bool enabled) {
    if (!g.hwnd || g.windowFullscreen == enabled) return;
    if (enabled) {
        g.windowedStyle = (DWORD)GetWindowLongPtrA(g.hwnd, GWL_STYLE);
        g.windowedPlacement.length = sizeof(WINDOWPLACEMENT);
        GetWindowPlacement(g.hwnd, &g.windowedPlacement);
        MONITORINFO mi = { sizeof(MONITORINFO) };
        GetMonitorInfoA(MonitorFromWindow(g.hwnd, MONITOR_DEFAULTTONEAREST), &mi);
        SetWindowLongPtrA(g.hwnd, GWL_STYLE, g.windowedStyle & ~WS_OVERLAPPEDWINDOW);
        SetWindowPos(g.hwnd, HWND_TOP, mi.rcMonitor.left, mi.rcMonitor.top,
                     mi.rcMonitor.right - mi.rcMonitor.left,
                     mi.rcMonitor.bottom - mi.rcMonitor.top,
                     SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
    } else {
        SetWindowLongPtrA(g.hwnd, GWL_STYLE, g.windowedStyle);
        SetWindowPlacement(g.hwnd, &g.windowedPlacement);
        SetWindowPos(g.hwnd, nullptr, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER |
                     SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
    }
    g.windowFullscreen = enabled;
}

static void toggleEditorFullscreen() {
    g.editorFullscreen = !g.editorFullscreen;
    // During a fullscreen Play session F11 changes the state the editor will
    // return to; the game itself remains fullscreen until Stop.
    if (!g.playFullscreenActive) setWindowFullscreen(g.editorFullscreen);
}

static std::string curDirAbs() {
    return g.curRel.empty() ? g.projectDir : g.projectDir + "\\" + g.curRel;
}

static int scanFolderRec(const std::string& rel, const std::string& name, int depth) {
    int idx = (int)g.folders.size();
    g.folders.push_back({ rel, name, {} });
    if (depth > 6) return idx;
    std::error_code ec;
    std::string abs = rel.empty() ? g.projectDir : g.projectDir + "\\" + rel;
    std::vector<std::string> subs;
    for (const auto& entry : fs::directory_iterator(abs, ec)) {
        if (entry.is_directory()) subs.push_back(entry.path().filename().string());
    }
    std::sort(subs.begin(), subs.end());
    for (const auto& s : subs) {
        std::string childRel = rel.empty() ? s : rel + "\\" + s;
        int child = scanFolderRec(childRel, s, depth + 1);
        g.folders[idx].kids.push_back(child);
    }
    return idx;
}

static std::string relJoin(const std::string& a, const std::string& b);
static void browserPruneSelection();

static void scanBrowser() {
    g.folders.clear();
    scanFolderRec("", g.projectName, 0);
    std::error_code ec;
    if (!g.curRel.empty() && !fs::exists(g.projectDir + "\\" + g.curRel, ec)) g.curRel = "";
    g.curDirs.clear();
    g.curPfbs.clear();
    g.curImps.clear();
    g.curBps.clear();
    g.curBpis.clear();
    g.curCurves.clear();
    g.curAnimations.clear();
    g.curAnimators.clear();
    g.curMeshes.clear();
    g.curTextures.clear();
    g.curAudio.clear();
    g.curAudioClasses.clear();
    g.curAudioAttenuations.clear();
    g.curAudioConcurrencies.clear();
    g.curEnums.clear();
    g.curMaterials.clear();
    g.curWidgets.clear();
    for (const auto& entry : fs::directory_iterator(curDirAbs(), ec)) {
        std::string name = entry.path().filename().string();
        if (entry.is_directory()) {
            g.curDirs.push_back(name);
        } else if (entry.is_regular_file()) {
            std::string ext = entry.path().extension().string();
            std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return (char)tolower(c); });
            if (ext == ".pfb") g.curPfbs.push_back(name);
            else if (ext == ".imp") g.curImps.push_back(name);
            else if (ext == ".bp") g.curBps.push_back(name);
            else if (ext == ".bpi") g.curBpis.push_back(name);
            else if (ext == ".curve") g.curCurves.push_back(name);
            else if (ext == ".anim") g.curAnimations.push_back(name);
            else if (ext == ".animctrl") g.curAnimators.push_back(name);
            else if (ext == ".obj" || ext == ".fbx" || ext == ".gltf" || ext == ".glb" ||
                     ext == ".dae" || ext == ".3ds" || ext == ".stl") g.curMeshes.push_back(name);
            else if (ext == ".png") g.curTextures.push_back(name);
            else if (ext == ".mp3" || ext == ".wav" || ext == ".ogg") g.curAudio.push_back(name);
            else if (ext == ".aclass") g.curAudioClasses.push_back(name);
            else if (ext == ".atten") g.curAudioAttenuations.push_back(name);
            else if (ext == ".concurrency") g.curAudioConcurrencies.push_back(name);
            else if (ext == ".enum") g.curEnums.push_back(name);
            else if (ext == ".mat") g.curMaterials.push_back(name);
            else if (ext == ".wgt") g.curWidgets.push_back(name);
        }
    }
    std::sort(g.curDirs.begin(), g.curDirs.end());
    std::sort(g.curPfbs.begin(), g.curPfbs.end());
    std::sort(g.curImps.begin(), g.curImps.end());
    std::sort(g.curBps.begin(), g.curBps.end());
    std::sort(g.curBpis.begin(), g.curBpis.end());
    std::sort(g.curCurves.begin(), g.curCurves.end());
    std::sort(g.curAnimations.begin(), g.curAnimations.end());
    std::sort(g.curAnimators.begin(), g.curAnimators.end());
    std::sort(g.curMeshes.begin(), g.curMeshes.end());
    std::sort(g.curTextures.begin(), g.curTextures.end());
    std::sort(g.curAudio.begin(), g.curAudio.end());
    std::sort(g.curAudioClasses.begin(), g.curAudioClasses.end());
    std::sort(g.curAudioAttenuations.begin(), g.curAudioAttenuations.end());
    std::sort(g.curAudioConcurrencies.begin(), g.curAudioConcurrencies.end());
    std::sort(g.curEnums.begin(), g.curEnums.end());
    std::sort(g.curMaterials.begin(), g.curMaterials.end());
    std::sort(g.curWidgets.begin(), g.curWidgets.end());
    g.projectMeshAssets.clear();
    g.projectMaterialAssets.clear();
    g.projectWidgetAssets.clear();
    for(const auto& asset:fs::recursive_directory_iterator(g.projectDir,fs::directory_options::skip_permission_denied,ec)){
        if(!asset.is_regular_file())continue;
        std::string ext=asset.path().extension().string();
        std::transform(ext.begin(),ext.end(),ext.begin(),[](unsigned char c){return(char)tolower(c);});
        std::error_code relError;fs::path rel=fs::relative(asset.path(),g.projectDir,relError);
        if(ext==".mat"){ if(!relError)g.projectMaterialAssets.push_back(rel.string()); continue; }
        if(ext==".wgt"){ if(!relError)g.projectWidgetAssets.push_back(rel.string()); continue; }
        if(ext!=".obj"&&ext!=".fbx"&&ext!=".gltf"&&ext!=".glb"&&ext!=".dae"&&ext!=".3ds"&&ext!=".stl")continue;
        if(!relError)g.projectMeshAssets.push_back(rel.string());
    }
    std::sort(g.projectMeshAssets.begin(),g.projectMeshAssets.end());
    std::sort(g.projectMaterialAssets.begin(),g.projectMaterialAssets.end());
    std::sort(g.projectWidgetAssets.begin(),g.projectWidgetAssets.end());
    g.bpEditCache.clear();
    g.curveCache.clear();
    g.audioClassCache.clear();
    g.audioAttenuationCache.clear();
    g.audioConcurrencyCache.clear();
    g.materialCache.clear();
    browserPruneSelection();
}

static void loadAssetIcons() {
    fs::path dir=fs::path(g.baseDir)/"assets"/"icons";std::error_code ec;
    for(const auto& entry:fs::directory_iterator(dir,ec)){
        if(!entry.is_regular_file()||_stricmp(entry.path().extension().string().c_str(),".png")!=0)continue;
        GLuint tex=g.renderer.loadPngTexture(entry.path().string());
        if(tex)g.assetIconTextures[entry.path().stem().string()]=tex;
    }
}

static const char* browserIconImage(const std::string& name,int icon){
    static std::string result;
    if(icon==0)return name==".."?"folder_open":"folder";
    if(icon==2)return "prefab";if(icon==3)return "level";
    if(icon==4){std::string data;int kind=0;std::string rel=relJoin(g.curRel,name);if(readFile(g.projectDir+"\\"+rel,data))sscanf(data.c_str(),"%*[^\\n]\\nclasskind %d",&kind);
        const char* kinds[]={"blueprint","blueprint_gamemode","blueprint_gameinstance","blueprint_playercontroller","blueprint_savegame"};
        return kinds[(std::max)(0,(std::min)(kind,4))];}
    if(icon==5)return "blueprint_interface";if(icon==6)return "curve";if(icon==8)return "texture";
    if(icon==10)return "audio_class";if(icon==11)return "audio_attenuation";if(icon==12)return "audio_concurrency";
    if(icon==13)return "enum";if(icon==14)return "animation_clip";if(icon==15)return "animator_controller";
    if(icon==16)return "material";if(icon==17)return "widget";
    std::string ext=fs::path(name).extension().string();std::transform(ext.begin(),ext.end(),ext.begin(),[](unsigned char c){return(char)tolower(c);});
    if(icon==7){result="mesh_"+(ext.empty()?std::string():ext.substr(1));return result.c_str();}
    if(icon==9){result="audio_"+(ext.empty()?std::string():ext.substr(1));return result.c_str();}
    return "generic_asset";
}

static std::string browserDisplayName(const std::string& name,int icon){
    if(icon==0||name=="..")return name;
    return fs::path(name).stem().string();
}

static std::string browserRelName(const std::string& rel) {
    return fs::path(rel).filename().string();
}

static void browserClearSelection() {
    g.browserSel.clear();
    g.browserSelected.clear();
    g.browserSelectionAnchor.clear();
}

static void browserSetActiveRel(const std::string& rel) {
    g.browserSel = rel.empty() ? std::string{} : browserRelName(rel);
}

static void browserSetSingleSelectionRel(const std::string& rel) {
    browserClearSelection();
    if (rel.empty()) return;
    g.browserSelected.insert(rel);
    g.browserSelectionAnchor = rel;
    browserSetActiveRel(rel);
}

static void browserSelectVisibleRel(const std::string& rel, const UIInput& input) {
    if (rel.empty()) return;
    if (input.keyShift && !g.browserSelectionAnchor.empty()) {
        auto a = std::find(g.browserVisibleOrder.begin(), g.browserVisibleOrder.end(), g.browserSelectionAnchor);
        auto b = std::find(g.browserVisibleOrder.begin(), g.browserVisibleOrder.end(), rel);
        if (a != g.browserVisibleOrder.end() && b != g.browserVisibleOrder.end()) {
            if (!input.keyCtrl) g.browserSelected.clear();
            int ia = (int)(a - g.browserVisibleOrder.begin());
            int ib = (int)(b - g.browserVisibleOrder.begin());
            if (ia > ib) std::swap(ia, ib);
            for (int i = ia; i <= ib; i++) g.browserSelected.insert(g.browserVisibleOrder[i]);
            browserSetActiveRel(rel);
            return;
        }
    }
    if (input.keyCtrl) {
        if (g.browserSelected.count(rel)) g.browserSelected.erase(rel);
        else g.browserSelected.insert(rel);
        browserSetActiveRel(g.browserSelected.count(rel) ? rel : (g.browserSelected.empty() ? std::string{} : *g.browserSelected.rbegin()));
        g.browserSelectionAnchor = rel;
        if (g.browserSelected.empty()) g.browserSelectionAnchor.clear();
        return;
    }
    browserSetSingleSelectionRel(rel);
}

static void browserPruneSelection() {
    std::set<std::string> visible;
    auto addVisible = [&](const std::string& name) { visible.insert(relJoin(g.curRel, name)); };
    for (const auto& name : g.curDirs) addVisible(name);
    for (const auto& name : g.curPfbs) addVisible(name);
    for (const auto& name : g.curImps) addVisible(name);
    for (const auto& name : g.curBps) addVisible(name);
    for (const auto& name : g.curBpis) addVisible(name);
    for (const auto& name : g.curCurves) addVisible(name);
    for (const auto& name : g.curMeshes) addVisible(name);
    for (const auto& name : g.curTextures) addVisible(name);
    for (const auto& name : g.curAudio) addVisible(name);
    for (const auto& name : g.curAudioClasses) addVisible(name);
    for (const auto& name : g.curAudioAttenuations) addVisible(name);
    for (const auto& name : g.curAudioConcurrencies) addVisible(name);
    for (const auto& name : g.curEnums) addVisible(name);
    for (const auto& name : g.curAnimations) addVisible(name);
    for (const auto& name : g.curAnimators) addVisible(name);
    for (const auto& name : g.curMaterials) addVisible(name);
    for (const auto& name : g.curWidgets) addVisible(name);

    for (auto it = g.browserSelected.begin(); it != g.browserSelected.end();) {
        if (!visible.count(*it)) it = g.browserSelected.erase(it);
        else ++it;
    }
    if (!g.browserSelectionAnchor.empty() && !visible.count(g.browserSelectionAnchor))
        g.browserSelectionAnchor.clear();
    if (!g.browserSel.empty() && !visible.count(relJoin(g.curRel, g.browserSel)))
        g.browserSel.clear();
    if (g.browserSelected.empty()) g.browserSelectionAnchor.clear();
}

// lazy edit-time load of a graph asset (for exposed variables in Details)
// ═══ blueprint documents (one open blueprint = one top-level tab) ═══
static BPEditor* activeBP() {
    if (g.activeDoc >= 1 && g.activeDoc <= (int)g.bpDocs.size()) return g.bpDocs[g.activeDoc - 1].get();
    return nullptr;
}
static CurveEditor* activeCurve() {
    int i = g.activeDoc - 1 - (int)g.bpDocs.size();
    return i >= 0 && i < (int)g.curveDocs.size() ? g.curveDocs[i].get() : nullptr;
}
static std::string bpDocTitle(const BPEditor* ed) {
    if (!ed || ed->curPath.empty()) return "Blueprint*";
    std::string t = fs::path(ed->curPath).stem().string();
    if (ed->dirty) t += "*";
    return t;
}
static const char* bpDocIcon(const BPEditor* ed) {
    if (!ed) return "blueprint";
    if (!ed->curPath.empty() && fs::path(ed->curPath).extension() == ".bpi") return "blueprint_interface";
    static const char* kinds[] = { "blueprint", "blueprint_gamemode", "blueprint_gameinstance",
                                   "blueprint_playercontroller", "blueprint_savegame" };
    int k = (int)ed->graph.classKind;
    if (k < 0) k = 0; else if (k > 4) k = 4;
    return kinds[k];
}
// open a blueprint as its own tab (or focus it if already open)
static int openBlueprintDoc(const std::string& absPath, const std::string& rel) {
    for (int i = 0; i < (int)g.bpDocs.size(); i++) {
        if (g.bpDocs[i]->curPath == rel) { g.activeDoc = i + 1; return i; }
    }
    auto ed = std::make_unique<BPEditor>();
    ed->projectDir = g.projectDir;
    ed->hwnd = g.hwnd;
    ed->logFn = addLog;
    if (!ed->loadFrom(absPath, rel)) { addLog(2, "Could not open the blueprint: %s", rel.c_str()); return -1; }
    g.bpDocs.push_back(std::move(ed));
    g.activeDoc = (int)g.bpDocs.size();
    return (int)g.bpDocs.size() - 1;
}
static BPEditor& newBlueprintDoc() {
    auto ed = std::make_unique<BPEditor>();
    ed->projectDir = g.projectDir;
    ed->hwnd = g.hwnd;
    ed->logFn = addLog;
    ed->newGraph();
    g.bpDocs.push_back(std::move(ed));
    g.activeDoc = (int)g.bpDocs.size();
    return *g.bpDocs.back();
}

static std::string curveDocTitle(const CurveEditor* ed) {
    if (!ed || ed->curPath.empty()) return "Curve*";
    std::string t = fs::path(ed->curPath).stem().string();
    if (ed->dirty) t += "*";
    return t;
}

static int openCurveDoc(const std::string& absPath, const std::string& rel) {
    for (int i = 0; i < (int)g.curveDocs.size(); i++) {
        if (g.curveDocs[i]->curPath == rel) {
            g.activeDoc = 1 + (int)g.bpDocs.size() + i;
            return i;
        }
    }
    auto ed = std::make_unique<CurveEditor>();
    ed->projectDir = g.projectDir;
    ed->logFn = addLog;
    if (!ed->loadFrom(absPath, rel)) { addLog(2, "Could not open the Curve: %s", rel.c_str()); return -1; }
    g.curveDocs.push_back(std::move(ed));
    g.activeDoc = (int)g.bpDocs.size() + (int)g.curveDocs.size();
    return (int)g.curveDocs.size() - 1;
}

// materials come after the curve tabs in the linear activeDoc index
static int materialDocBase() { return 1 + (int)g.bpDocs.size() + (int)g.curveDocs.size(); }
static MaterialEditor* activeMaterial() {
    int i = g.activeDoc - materialDocBase();
    return i >= 0 && i < (int)g.materialDocs.size() ? g.materialDocs[i].get() : nullptr;
}
static std::string materialDocTitle(const MaterialEditor* ed) {
    if (!ed || ed->curPath.empty()) return "Material*";
    std::string t = fs::path(ed->curPath).stem().string();
    if (ed->dirty) t += "*";
    return t;
}
static int openMaterialDoc(const std::string& absPath, const std::string& rel) {
    for (int i = 0; i < (int)g.materialDocs.size(); i++) {
        if (g.materialDocs[i]->curPath == rel) { g.activeDoc = materialDocBase() + i; return i; }
    }
    auto ed = std::make_unique<MaterialEditor>();
    ed->projectDir = g.projectDir;
    ed->logFn = addLog;
    if (!ed->loadFrom(absPath, rel)) { addLog(2, "Could not open the Material: %s", rel.c_str()); return -1; }
    g.materialDocs.push_back(std::move(ed));
    g.activeDoc = materialDocBase() + (int)g.materialDocs.size() - 1;
    return (int)g.materialDocs.size() - 1;
}

// widgets come after the material tabs in the linear activeDoc index
static int widgetDocBase() { return materialDocBase() + (int)g.materialDocs.size(); }
static WidgetEditor* activeWidget() {
    int i = g.activeDoc - widgetDocBase();
    return i >= 0 && i < (int)g.widgetDocs.size() ? g.widgetDocs[i].get() : nullptr;
}
static std::string widgetDocTitle(const WidgetEditor* ed) {
    if (!ed || ed->curPath.empty()) return "Widget*";
    std::string t = fs::path(ed->curPath).stem().string();
    if (ed->isDirty()) t += "*";
    return t;
}
static int openWidgetDoc(const std::string& absPath, const std::string& rel) {
    for (int i = 0; i < (int)g.widgetDocs.size(); i++) {
        if (g.widgetDocs[i]->curPath == rel) { g.activeDoc = widgetDocBase() + i; return i; }
    }
    auto ed = std::make_unique<WidgetEditor>();
    ed->projectDir = g.projectDir;
    ed->logFn = addLog;
    if (!ed->loadFrom(absPath, rel)) { addLog(2, "Could not open the Widget: %s", rel.c_str()); return -1; }
    g.widgetDocs.push_back(std::move(ed));
    g.activeDoc = widgetDocBase() + (int)g.widgetDocs.size() - 1;
    return (int)g.widgetDocs.size() - 1;
}

static BPGraph* editGraph(const char* relPath) {
    // any open blueprint tab is served live: new/removed variables show up in the
    // Details immediately, unsaved edits included
    for (auto& ed : g.bpDocs) {
        if (!ed->curPath.empty() && ed->curPath == relPath) return &ed->graph;
    }
    auto it = g.bpEditCache.find(relPath);
    if (it != g.bpEditCache.end()) return &it->second;
    std::string data;
    if (!readFile(g.projectDir + "\\" + relPath, data)) return nullptr;
    BPGraph gph;
    if (!gph.deserialize(data)) return nullptr;
    return &g.bpEditCache.emplace(relPath, std::move(gph)).first->second;
}

// ═══ euler helpers ═══
// inverse of Quat::fromEulerDeg: returns { X = roll, Y = pitch, Z = yaw }
static Vec3 quatToEulerDeg(const Quat& q) {
    float x = q.x, y = q.y, z = q.z, w = q.w;
    float sinp = 2 * (w * x - y * z);          // sin(pitch)
    float roll, pitch, yaw;
    if (fabsf(sinp) < 0.9999f) {
        pitch = asinf(sinp);
        yaw = atan2f(2 * (x * z + w * y), 1 - 2 * (x * x + y * y));
        roll = atan2f(2 * (x * y + w * z), 1 - 2 * (x * x + z * z));
    } else {
        pitch = sinp > 0 ? PI / 2 : -PI / 2;   // gimbal: fold roll into yaw
        yaw = atan2f(-2 * (x * z - w * y), 1 - 2 * (y * y + z * z));
        roll = 0;
    }
    const float r = 180.0f / PI;
    return { roll * r, pitch * r, yaw * r };
}

// --- baked navigation grid -------------------------------------------------
static int navIndex(int x, int z) { return z * g.navigation.width + x; }
static void invalidateNavigation() {
    if (!g.navigation.baked) return;
    g.navigation.baked = false;
    g.navigation.status = "Geometry changed: run the Bake again.";
}
static bool navWalkable(int x, int z) {
    return x >= 0 && z >= 0 && x < g.navigation.width && z < g.navigation.height &&
           std::isfinite(g.navigation.cells[navIndex(x, z)]);
}
static Vec3 navCellPosition(int index) {
    int x = index % g.navigation.width, z = index / g.navigation.width;
    return { g.navigation.origin.x + (x + 0.5f) * g.navigation.cellSize,
             g.navigation.cells[index],
             g.navigation.origin.z + (z + 0.5f) * g.navigation.cellSize };
}

static void bakeNavigation() {
    auto& nav = g.navigation;
    nav.baked = false;
    nav.cells.clear();
    nav.sourceCount = 0;
    nav.occluderCount = 0;
    Vec3 mn{ 1e30f, 1e30f, 1e30f }, mx{ -1e30f, -1e30f, -1e30f };
    for (Entity& e : g.scene.entities) {
        if (!(e.staticFlags & STATIC_NAVIGATION) || !e.hasMesh || !e.body) continue;
        e.body->updateAABB();
        mn.x = (std::min)(mn.x, e.body->aabb.min.x); mn.z = (std::min)(mn.z, e.body->aabb.min.z);
        mx.x = (std::max)(mx.x, e.body->aabb.max.x); mx.z = (std::max)(mx.z, e.body->aabb.max.z);
        nav.sourceCount++;
    }
    for (Entity& e : g.scene.entities) if (e.hasNavigationOccluder && e.body) {
        e.body->updateAABB();
        nav.occluderCount++;
    }
    if (!nav.sourceCount) {
        nav.status = "No object with Static > Navigation.";
        addLog(2, "%s", nav.status.c_str());
        return;
    }
    float margin = nav.cellSize * 2.0f;
    mn.x -= margin; mn.z -= margin; mx.x += margin; mx.z += margin;
    float requestedCell = clampf(nav.cellSize, 0.1f, 5.0f);
    int w = (int)ceilf((mx.x - mn.x) / requestedCell);
    int h = (int)ceilf((mx.z - mn.z) / requestedCell);
    float scale = (std::max)(w / 160.0f, h / 160.0f);
    if (scale > 1.0f) requestedCell *= scale;
    nav.cellSize = requestedCell;
    nav.width = (std::max)(1, (int)ceilf((mx.x - mn.x) / nav.cellSize));
    nav.height = (std::max)(1, (int)ceilf((mx.z - mn.z) / nav.cellSize));
    nav.origin = { mn.x, 0, mn.z };
    nav.cells.assign((size_t)nav.width * nav.height, std::numeric_limits<float>::quiet_NaN());
    for (int z = 0; z < nav.height; z++) for (int x = 0; x < nav.width; x++) {
        float wx = nav.origin.x + (x + 0.5f) * nav.cellSize;
        float wz = nav.origin.z + (z + 0.5f) * nav.cellSize;
        float top = -1e30f;
        for (const Entity& e : g.scene.entities) {
            if (!(e.staticFlags & STATIC_NAVIGATION) || !e.hasMesh || !e.body) continue;
            const AABB& a = e.body->aabb;
            if (wx >= a.min.x && wx <= a.max.x && wz >= a.min.z && wz <= a.max.z)
                top = (std::max)(top, a.max.y);
        }
        if (top > -1e20f) nav.cells[navIndex(x, z)] = top;
    }

    // Explicit occluders subtract their mesh/trigger footprint from the surface.
    for (const Entity& e : g.scene.entities) {
        if (!e.hasNavigationOccluder || !e.body) continue;
        const AABB& a = e.body->aabb;
        const float padding = (std::max)(0.0f, e.navigationOccluderPadding);
        for (int z = 0; z < nav.height; z++) for (int x = 0; x < nav.width; x++) {
            int idx = navIndex(x, z);
            if (!std::isfinite(nav.cells[idx])) continue;
            float wx = nav.origin.x + (x + 0.5f) * nav.cellSize;
            float wz = nav.origin.z + (z + 0.5f) * nav.cellSize;
            float surface = nav.cells[idx];
            if (wx >= a.min.x - padding && wx <= a.max.x + padding &&
                wz >= a.min.z - padding && wz <= a.max.z + padding &&
                surface <= a.max.y + nav.stepHeight && surface >= a.min.y - nav.stepHeight)
                nav.cells[idx] = std::numeric_limits<float>::quiet_NaN();
        }
    }

    // Erode the walkable area by the agent radius. This keeps paths away from
    // mesh borders and obstacle faces instead of letting the centre skim them.
    const int clearanceCells = (int)ceilf((std::max)(0.0f, nav.agentRadius) / nav.cellSize);
    if (clearanceCells > 0) {
        std::vector<float> original = nav.cells;
        for (int z = 0; z < nav.height; z++) for (int x = 0; x < nav.width; x++) {
            int idx = navIndex(x, z);
            if (!std::isfinite(original[idx])) continue;
            bool blocked = false;
            for (int dz = -clearanceCells; dz <= clearanceCells && !blocked; dz++)
                for (int dx = -clearanceCells; dx <= clearanceCells; dx++) {
                    if (dx * dx + dz * dz > clearanceCells * clearanceCells) continue;
                    int nx = x + dx, nz = z + dz;
                    if (nx < 0 || nz < 0 || nx >= nav.width || nz >= nav.height ||
                        !std::isfinite(original[nz * nav.width + nx])) { blocked = true; break; }
                }
            if (blocked) nav.cells[idx] = std::numeric_limits<float>::quiet_NaN();
        }
    }
    nav.baked = true;
    char status[160];
    snprintf(status, sizeof(status), "Bake complete: %d surfaces, %d occluders, %dx%d cells; margin %.2f m.",
             nav.sourceCount, nav.occluderCount, nav.width, nav.height, nav.agentRadius);
    nav.status = status;
    for (Entity& e : g.scene.entities) if (e.hasAIAgent) { e.aiHasPath = false; e.aiRepathTimer = 0; }
    addLog(1, "%s", nav.status.c_str());
}

static int navNearestCell(const Vec3& position) {
    if (!g.navigation.baked) return -1;
    int best = -1;
    float bestDistance = 1e30f;
    for (int i = 0; i < (int)g.navigation.cells.size(); i++) {
        if (!std::isfinite(g.navigation.cells[i])) continue;
        Vec3 p = navCellPosition(i);
        float dx = p.x - position.x, dz = p.z - position.z;
        float score = dx * dx + dz * dz + 0.15f * (p.y - position.y) * (p.y - position.y);
        if (score < bestDistance) { bestDistance = score; best = i; }
    }
    return best;
}

static bool findNavigationPath(const Vec3& start, const Vec3& destination, std::vector<Vec3>& path) {
    path.clear();
    auto& nav = g.navigation;
    int begin = navNearestCell(start), goal = navNearestCell(destination);
    if (begin < 0 || goal < 0) return false;
    int count = nav.width * nav.height;
    std::vector<float> cost(count, 1e30f);
    std::vector<int> parent(count, -1);
    using Entry = std::pair<float, int>;
    std::priority_queue<Entry, std::vector<Entry>, std::greater<Entry>> open;
    cost[begin] = 0; open.push({ 0.0f, begin });
    static const int DX[8] = { 1,-1,0,0,1,1,-1,-1 };
    static const int DZ[8] = { 0,0,1,-1,1,-1,1,-1 };
    while (!open.empty()) {
        int current = open.top().second; open.pop();
        if (current == goal) break;
        int cx = current % nav.width, cz = current / nav.width;
        for (int n = 0; n < 8; n++) {
            int nx = cx + DX[n], nz = cz + DZ[n];
            if (!navWalkable(nx, nz)) continue;
            if (n >= 4 && (!navWalkable(cx + DX[n], cz) || !navWalkable(cx, cz + DZ[n]))) continue;
            int next = navIndex(nx, nz);
            float dh = fabsf(nav.cells[next] - nav.cells[current]);
            if (dh > nav.stepHeight) continue;
            float step = n < 4 ? 1.0f : 1.41421356f;
            float nextCost = cost[current] + step + dh * 2.0f;
            if (nextCost >= cost[next]) continue;
            cost[next] = nextCost; parent[next] = current;
            int gx = goal % nav.width, gz = goal / nav.width;
            float heuristic = sqrtf((float)((gx - nx) * (gx - nx) + (gz - nz) * (gz - nz)));
            open.push({ nextCost + heuristic, next });
        }
    }
    if (begin != goal && parent[goal] < 0) return false;
    std::vector<int> reverse;
    for (int p = goal; p >= 0; p = parent[p]) { reverse.push_back(p); if (p == begin) break; }
    std::vector<int> raw(reverse.rbegin(), reverse.rend());
    auto clearLine = [&](int a, int b) {
        Vec3 from = navCellPosition(a), to = navCellPosition(b);
        float planar = sqrtf((to.x-from.x)*(to.x-from.x)+(to.z-from.z)*(to.z-from.z));
        int samples = (std::max)(1, (int)ceilf(planar / (nav.cellSize * 0.3f)));
        float previousY = from.y;
        for (int i = 1; i <= samples; i++) {
            float t = (float)i / samples;
            float wx = from.x + (to.x-from.x)*t, wz = from.z + (to.z-from.z)*t;
            int x = (int)floorf((wx-nav.origin.x)/nav.cellSize);
            int z = (int)floorf((wz-nav.origin.z)/nav.cellSize);
            if (!navWalkable(x,z)) return false;
            float y = nav.cells[navIndex(x,z)];
            if (fabsf(y-previousY)>nav.stepHeight) return false;
            previousY=y;
        }
        return true;
    };
    // String-pull the cell path: retain only corners that are actually needed.
    for (size_t i = 0; i < raw.size();) {
        path.push_back(navCellPosition(raw[i]));
        if (i + 1 >= raw.size()) break;
        size_t furthest = i + 1;
        for (size_t j = i + 2; j < raw.size(); j++) {
            if (!clearLine(raw[i], raw[j])) break;
            furthest = j;
        }
        i = furthest;
    }
    if (path.empty()) return false;
    path.back() = { destination.x, path.back().y, destination.z };
    // Chaikin corner-cutting: turn the grid-corner path into a smooth, Unity/Unreal-
    // style curve instead of stair-stepping between cell centres. Endpoints are pinned;
    // each cut point is kept only where the navmesh is still walkable, so the rounding
    // never lets the path clip across the inside of an obstacle corner.
    auto navPointWalkable = [&](const Vec3& p) {
        int x = (int)floorf((p.x - nav.origin.x) / nav.cellSize);
        int z = (int)floorf((p.z - nav.origin.z) / nav.cellSize);
        return navWalkable(x, z);
    };
    for (int pass = 0; pass < 2 && path.size() >= 3; pass++) {
        std::vector<Vec3> out;
        out.reserve(path.size() * 2);
        out.push_back(path.front());
        for (size_t i = 0; i + 1 < path.size(); i++) {
            Vec3 a = path[i], b = path[i + 1];
            Vec3 q = a * 0.75f + b * 0.25f;
            Vec3 r2 = a * 0.25f + b * 0.75f;
            out.push_back(navPointWalkable(q) ? q : a);
            out.push_back(navPointWalkable(r2) ? r2 : b);
        }
        out.push_back(path.back());
        path.swap(out);
    }
    return true;
}

static void aiSetTarget(Entity& agent, int targetEntity) {
    agent.hasAIAgent = true; agent.aiTargetEntity = targetEntity; agent.aiUseTargetEntity = targetEntity != 0;
    agent.aiStopped = false; agent.aiHasPath = false; agent.aiRepathTimer = 0;
}
static void aiSetDestination(Entity& agent, const Vec3& destination) {
    agent.hasAIAgent = true; agent.aiDestination = destination; agent.aiUseTargetEntity = false;
    agent.aiStopped = false; agent.aiHasPath = false; agent.aiRepathTimer = 0;
}

static void updateAIAgents(float dt) {
    if (!g.navigation.baked) return;
    for (Entity& agent : g.scene.entities) {
        if (!agent.hasAIAgent || !agent.body || agent.aiStopped || (agent.staticFlags & STATIC_MOVEMENT)) continue;
        Vec3 destination = agent.aiDestination;
        if (agent.aiUseTargetEntity) {
            Entity* target = g.scene.byId(agent.aiTargetEntity);
            if (!target || !target->body) { agent.aiHasPath = false; continue; }
            destination = target->body->position;
        }
        agent.aiRepathTimer -= dt;
        Vec3 targetDelta = destination - agent.aiLastPathTarget;
        Vec3 directDelta{destination.x-agent.body->position.x,0,destination.z-agent.body->position.z};
        float arrivalDistance=(std::max)(0.03f,agent.aiStoppingDistance);
        if(directDelta.length()<=arrivalDistance){
            agent.aiHasPath=false;agent.aiRemainingDistance=0;agent.aiPath.clear();
            agent.aiSteeringVelocity={};
            if(agent.body->type==BodyType::Dynamic){agent.body->velocity.x=0;agent.body->velocity.z=0;}
            continue;
        }
        float targetMoveThreshold=g.navigation.cellSize*0.65f;
        bool targetMoved=targetDelta.x*targetDelta.x+targetDelta.z*targetDelta.z > targetMoveThreshold*targetMoveThreshold;
        bool needsPath=(!agent.aiHasPath&&agent.aiRepathTimer<=0)||(targetMoved&&agent.aiRepathTimer<=0);
        if (needsPath) {
            agent.aiHasPath = findNavigationPath(agent.body->position, destination, agent.aiPath);
            agent.aiPathIndex = agent.aiPath.size() > 1 ? 1 : 0;
            agent.aiLastPathTarget = destination;
            agent.aiRepathTimer = 0.40f;
        }
        if (!agent.aiHasPath || agent.aiPathIndex >= (int)agent.aiPath.size()) continue;
        agent.aiRemainingDistance = 0;
        Vec3 previous = agent.body->position;
        for (int i = agent.aiPathIndex; i < (int)agent.aiPath.size(); i++) {
            agent.aiRemainingDistance += (agent.aiPath[i] - previous).length();
            previous = agent.aiPath[i];
        }
        Vec3 waypoint = agent.aiPath[agent.aiPathIndex];
        Vec3 delta = { waypoint.x - agent.body->position.x, 0, waypoint.z - agent.body->position.z };
        float distance = delta.length();
        bool finalWaypoint = agent.aiPathIndex + 1 >= (int)agent.aiPath.size();
        float reach = finalWaypoint ? (std::max)(0.03f, agent.aiStoppingDistance)
                                    : (std::max)(g.navigation.cellSize * 0.45f, agent.aiStoppingDistance);
        if (distance <= reach) {
            agent.aiPathIndex++;
            if (agent.aiPathIndex >= (int)agent.aiPath.size()) {
                agent.aiHasPath = false; agent.aiRemainingDistance = 0;
                agent.aiSteeringVelocity = {};
                agent.body->velocity.x = agent.body->velocity.z = 0;
            }
            continue;
        }
        Vec3 direction = delta * (1.0f / distance);
        float speed = (std::max)(0.0f, agent.aiSpeed);
        if (finalWaypoint) {
            float slowRadius = (std::max)(g.navigation.cellSize * 1.5f, agent.aiStoppingDistance + 0.2f);
            speed *= clampf(distance / slowRadius, 0.12f, 1.0f);
        }
        if (agent.body->type == BodyType::Dynamic && agent.hasPhysics) {
            Vec3 desired = direction * speed;
            float maxChange = (std::max)(0.0f, agent.aiAcceleration) * dt;
            Vec3 current{ agent.body->velocity.x, 0, agent.body->velocity.z };
            Vec3 change = desired - current;
            if (change.length() > maxChange && maxChange > 0) change = change.normalized() * maxChange;
            agent.body->velocity.x += change.x; agent.body->velocity.z += change.z;
            agent.body->wake();
        } else {
            Vec3 desired = direction * speed;
            float maxChange = (std::max)(0.0f, agent.aiAcceleration) * dt;
            Vec3 change = desired - agent.aiSteeringVelocity;
            if (change.length() > maxChange && maxChange > 0) change = change.normalized() * maxChange;
            agent.aiSteeringVelocity += change;
            float move = (std::min)(distance, agent.aiSteeringVelocity.length() * dt);
            Vec3 moveDirection = agent.aiSteeringVelocity.lengthSq() > 0.000001f ? agent.aiSteeringVelocity.normalized() : direction;
            agent.body->position += moveDirection * move;
            agent.body->position.y = waypoint.y + agent.aiBaseOffset;
            agent.body->updateAABB();
        }
        if (agent.aiAngularSpeed > 0.0f) {
            float desiredYaw = atan2f(-direction.x, -direction.z) / DEG2RAD;
            Vec3 euler = quatToEulerDeg(agent.body->quat);
            float diff = desiredYaw - euler.z;
            while (diff > 180) diff -= 360; while (diff < -180) diff += 360;
            float maxTurn = agent.aiAngularSpeed * dt;
            euler.z += clampf(diff, -maxTurn, maxTurn);
            agent.body->quat = Quat::fromEulerDeg(euler.x, euler.y, euler.z).normalized();
            agent.body->updateAABB();
        }
    }
}

// ═══ viewport helpers ═══
static UIRect viewportRect() {
    if (g.standaloneMode || g.playFullscreenActive)
        return { 0, MENUBAR_H, (float)g.width, (float)g.height - MENUBAR_H };
    // the viewport exists only in the Level document; blueprints are fullscreen
    if (g.activeDoc != 0) return {};
    // reserve the bottom status bar: the dock lays out within [TOP_H, height-BAR]
    return g.dock.viewportRect(g.width, g.height - (int)BOTTOM_BAR_H, TOP_H);
}

static bool mouseInViewport() {
    UIRect v = viewportRect();
    return g.mouseX >= v.x && g.mouseX < v.x + v.w && g.mouseY >= v.y && g.mouseY < v.y + v.h;
}

// the full-viewport rect every runtime widget is laid out in (drawing and hit-testing)
static UIRect playWidgetRect() {
    UIRect v = viewportRect();
    if (v.w < 4 || v.h < 4) v = { 0, TOP_H, (float)g.width, (float)g.height - TOP_H };
    return v;
}

static void mouseRay(Vec3& origin, Vec3& dir) {
    UIRect v = viewportRect();
    if (v.w <= 0 || v.h <= 0) v = { 0, 0, (float)g.width, (float)g.height };
    float ndcX = (g.mouseX - v.x) / v.w * 2 - 1;
    float ndcY = -((g.mouseY - v.y) / v.h * 2 - 1);
    g.camera.screenRay(ndcX, ndcY, origin, dir);
}

// ═══ gizmo ═══
static const Vec3 GIZMO_AXES[3] = { { 1, 0, 0 }, { 0, 1, 0 }, { 0, 0, 1 } };
static const Vec3 GIZMO_COLORS[3] = { { 1.0f, 0.055f, 0.075f }, { 0.08f, 1.0f, 0.18f }, { 0.055f, 0.28f, 1.0f } };
static const Vec3 GIZMO_ROTATE_COLORS[3] = { { .88f,.20f,.22f }, { .20f,.78f,.30f }, { .18f,.38f,.86f } };

static Vec3 gizmoAxisFor(const Quat& rotation, int index) {
    return g.gizmoLocal ? rotation.rotate(GIZMO_AXES[index]).normalized() : GIZMO_AXES[index];
}

static float gizmoSize(const Vec3& pos) {
    float d = g.camera.eye.distanceTo(pos) * 0.15f;
    return d < 0.4f ? 0.4f : d;
}

static float axisParam(const Vec3& rayO, const Vec3& rayD, const Vec3& origin, const Vec3& axis) {
    Vec3 r = origin - rayO;
    float b = axis.dot(rayD);
    float c = axis.dot(r);
    float e = rayD.dot(r);
    float denom = 1 - b * b;
    if (fabsf(denom) < 1e-6f) return 0;
    return (b * e - c) / denom;
}

static bool rayPlaneVector(const Vec3& rayO,const Vec3& rayD,const Vec3& center,const Vec3& normal,Vec3& vector){
    float denom=normal.dot(rayD);if(fabsf(denom)<1e-6f)return false;
    float t=normal.dot(center-rayO)/denom;if(t<=0)return false;
    vector=rayO+rayD*t-center;float len=vector.length();if(len<1e-5f)return false;
    vector=vector*(1.0f/len);return true;
}

static int gizmoHitTest(const Vec3& rayO, const Vec3& rayD, const Vec3& pos, const Quat& rotation) {
    float s = gizmoSize(pos);
    if(g.gizmoMode==1){
        int best=-1;float bestError=1e30f;
        for(int i=0;i<3;i++){
            Vec3 axis=gizmoAxisFor(rotation,i),u=gizmoAxisFor(rotation,(i+1)%3),v=gizmoAxisFor(rotation,(i+2)%3),vec;
            float denom=axis.dot(rayD);if(fabsf(denom)<1e-6f)continue;
            float rt=axis.dot(pos-rayO)/denom;if(rt<=0)continue;
            Vec3 p=rayO+rayD*rt-pos;float radius=p.length(),pu=p.dot(u),pv=p.dot(v);
            // Unreal-like 90 degree arc in the positive quadrant between axes.
            if(pu<0||pv<0)continue;
            float error=fabsf(radius-s*.82f);
            if(error<s*.13f&&error<bestError){bestError=error;best=i;}
        }
        return best;
    }
    int best = -1;
    float bestT = 1e30f;
    for (int i = 0; i < 3; i++) {
        Vec3 axis = gizmoAxisFor(rotation, i);
        float t = clampf(axisParam(rayO, rayD, pos, axis), 0, s * 1.05f);
        Vec3 onAxis = pos + axis * t;
        Vec3 w = onAxis - rayO;
        float rayT = w.dot(rayD);
        if (rayT <= 0) continue;
        Vec3 onRay = rayO + rayD * rayT;
        if (onAxis.distanceTo(onRay) < s * 0.12f && rayT < bestT) {
            bestT = rayT;
            best = i;
        }
    }
    return best;
}

static void buildGizmoOverlay(Frame& f, const Vec3& pos, const Quat& objectRotation) {
    float s = gizmoSize(pos);
    if(g.gizmoMode==1){
        const int SEG=24;
        // Dashed X/Y/Z references keep the quarter arcs visually anchored in
        // 3D space without competing with the active rotation handle.
        for(int axisIndex=0;axisIndex<3;axisIndex++){
            Vec3 axis=gizmoAxisFor(objectRotation,axisIndex);
            Vec3 dashColor=GIZMO_ROTATE_COLORS[axisIndex]*.58f;
            const int DASHES=8;
            for(int side=-1;side<=1;side+=2)for(int k=0;k<DASHES;k++){
                float a=(.12f+k*.12f)*side,b=(.18f+k*.12f)*side;
                f.linesOverlay.push_back({pos+axis*s*a,dashColor});
                f.linesOverlay.push_back({pos+axis*s*b,dashColor});
            }
        }
        for(int axisIndex=0;axisIndex<3;axisIndex++){
            Vec3 u=gizmoAxisFor(objectRotation,(axisIndex+1)%3),v=gizmoAxisFor(objectRotation,(axisIndex+2)%3);
            bool hi=g.gizmoAxis==axisIndex||(g.gizmoAxis==-1&&g.hoverAxis==axisIndex);
            Vec3 color=hi?Vec3{1,.78f,.16f}:GIZMO_ROTATE_COLORS[axisIndex];
            for(int k=0;k<SEG;k++){
                float a=(PI*.5f)*k/SEG,b=(PI*.5f)*(k+1)/SEG;
                for(float band:{.80f,.82f,.84f}){
                    f.linesOverlayThick.push_back({pos+(u*cosf(a)+v*sinf(a))*s*band,color});
                    f.linesOverlayThick.push_back({pos+(u*cosf(b)+v*sinf(b))*s*band,color});
                }
            }
            if(g.transformSnap&&g.rotateSnap>0){
                Vec3 pinColor=hi?Vec3{1,1,.45f}:color*.82f+Vec3{.18f,.18f,.18f};
                auto addPin=[&](float degrees){
                    float a=degrees*DEG2RAD;Vec3 radial=u*cosf(a)+v*sinf(a);
                    f.linesOverlayThick.push_back({pos+radial*s*.72f,pinColor});
                    f.linesOverlayThick.push_back({pos+radial*s*.92f,pinColor});
                };
                addPin(0);
                for(float degrees=g.rotateSnap;degrees<90.0f-.001f;degrees+=g.rotateSnap)addPin(degrees);
                addPin(90);
            }
        }
        if(g.gizmoAxis>=0){
            DrawItem start;start.mesh=MESH_SPHERE;
            start.model=Mat4::compose(pos+g.gizmoRotationStartVector*s*.82f,{}, {s*.075f,s*.075f,s*.075f});
            start.color={1,.92f,.12f};start.emissive=.45f;f.overlay.push_back(start);
        }
        DrawItem center;center.mesh=MESH_SPHERE;center.model=Mat4::compose(pos,{}, {s*.065f,s*.065f,s*.065f});
        center.color={.95f,.95f,1};f.overlay.push_back(center);return;
    }
    const Quat ROTS[3] = {
        Quat::axisAngle({ 0, 0, 1 }, -PI / 2),
        Quat{},
        Quat::axisAngle({ 1, 0, 0 }, PI / 2),
    };
    for (int i = 0; i < 3; i++) {
        Vec3 axis = gizmoAxisFor(objectRotation, i);
        Quat axisRotation = g.gizmoLocal ? (objectRotation * ROTS[i]).normalized() : ROTS[i];
        bool hi = g.gizmoAxis == i || (g.gizmoAxis == -1 && g.hoverAxis == i);
        Vec3 color = hi ? Vec3{ 1, 0.85f, 0.2f } : GIZMO_COLORS[i];
        DrawItem shaft;
        shaft.mesh = MESH_CYLINDER;
        shaft.model = Mat4::compose(pos + axis * (s * 0.4f), axisRotation, { s * 0.035f, s * 0.8f, s * 0.035f });
        shaft.color = color;
        f.overlay.push_back(shaft);
        DrawItem head;
        head.mesh = g.gizmoMode == 2 ? MESH_CUBE : g.gizmoMode == 1 ? MESH_SPHERE : MESH_CONE;
        Vec3 hs = g.gizmoMode == 0 ? Vec3{ s * 0.14f, s * 0.26f, s * 0.14f } : Vec3{ s * 0.17f, s * 0.17f, s * 0.17f };
        head.model = Mat4::compose(pos + axis * (s * 0.9f), axisRotation, hs);
        head.color = color;
        f.overlay.push_back(head);
    }
    DrawItem center;
    center.mesh = MESH_CUBE;
    center.model = Mat4::compose(pos, {}, { s * 0.07f, s * 0.07f, s * 0.07f });
    center.color = { 0.9f, 0.9f, 0.95f };
    f.overlay.push_back(center);
}

// ═══ blueprint runtime glue ═══
static bool readFile(const std::string& path, std::string& out);

static int entityBlueprintCount(const Entity& entity) {
    return (entity.graphPath[0] ? 1 : 0) + (int)entity.additionalBlueprints.size();
}

static const char* entityBlueprintPath(const Entity& entity, int componentIndex) {
    if (componentIndex == 0 && entity.graphPath[0]) return entity.graphPath;
    int additionalIndex = componentIndex - (entity.graphPath[0] ? 1 : 0);
    if (additionalIndex < 0 || additionalIndex >= (int)entity.additionalBlueprints.size()) return "";
    return entity.additionalBlueprints[additionalIndex].graphPath.c_str();
}

static bool setEntityBlueprintPath(Entity& entity, int componentIndex, const std::string& path) {
    if (componentIndex == 0 && entity.graphPath[0]) {
        snprintf(entity.graphPath, sizeof(entity.graphPath), "%s", path.c_str());
        return true;
    }
    int additionalIndex = componentIndex - (entity.graphPath[0] ? 1 : 0);
    if (additionalIndex < 0 || additionalIndex >= (int)entity.additionalBlueprints.size()) return false;
    entity.additionalBlueprints[additionalIndex].graphPath = path;
    return true;
}

static std::map<std::string, Vec3>* entityBlueprintOverrides(Entity& entity, int componentIndex) {
    if (componentIndex == 0 && entity.graphPath[0]) return &entity.varOverrides;
    int additionalIndex = componentIndex - (entity.graphPath[0] ? 1 : 0);
    return additionalIndex >= 0 && additionalIndex < (int)entity.additionalBlueprints.size()
         ? &entity.additionalBlueprints[additionalIndex].varOverrides : nullptr;
}

static std::map<std::string, float>* entityBlueprintAlphaOverrides(Entity& entity, int componentIndex) {
    if (componentIndex == 0 && entity.graphPath[0]) return &entity.varAlphaOverrides;
    int additionalIndex = componentIndex - (entity.graphPath[0] ? 1 : 0);
    return additionalIndex >= 0 && additionalIndex < (int)entity.additionalBlueprints.size()
         ? &entity.additionalBlueprints[additionalIndex].varAlphaOverrides : nullptr;
}

// Move a blueprint component within the entity's list. Slot 0 lives in the
// legacy graphPath/override fields, so the whole list is unpacked, reordered and
// written back — keeping slot 0 authoritative for old scenes and framework code.
static void moveEntityBlueprint(Entity& entity, int from, int to) {
    int count = entityBlueprintCount(entity);
    if (from < 0 || from >= count || to < 0 || to > count || from == to) return;
    std::vector<BlueprintComponentDef> all;
    all.reserve(count);
    for (int i = 0; i < count; i++) {
        BlueprintComponentDef d;
        d.graphPath = entityBlueprintPath(entity, i);
        if (auto* v = entityBlueprintOverrides(entity, i)) d.varOverrides = *v;
        if (auto* a = entityBlueprintAlphaOverrides(entity, i)) d.varAlphaOverrides = *a;
        d.collapsed = i == 0 ? (entity.detailCollapsed & (1u << DETAIL_BLUEPRINT)) != 0
                             : entity.additionalBlueprints[i - 1].collapsed;
        all.push_back(std::move(d));
    }
    BlueprintComponentDef moved = std::move(all[from]);
    all.erase(all.begin() + from);
    if (to > from) to--;                       // indices shift once the source is pulled out
    all.insert(all.begin() + (to > (int)all.size() ? (int)all.size() : to), std::move(moved));
    snprintf(entity.graphPath, sizeof(entity.graphPath), "%s", all[0].graphPath.c_str());
    entity.varOverrides = all[0].varOverrides;
    entity.varAlphaOverrides = all[0].varAlphaOverrides;
    if (all[0].collapsed) entity.detailCollapsed |= 1u << DETAIL_BLUEPRINT;
    else entity.detailCollapsed &= ~(1u << DETAIL_BLUEPRINT);
    entity.additionalBlueprints.assign(all.begin() + 1, all.end());
}

static bool entityHasExactBlueprint(const Entity& entity, const std::string& relativePath) {
    for (int i = 0; i < entityBlueprintCount(entity); i++)
        if (_stricmp(entityBlueprintPath(entity, i), relativePath.c_str()) == 0) return true;
    return false;
}

static void applyRequiredNativeComponent(Entity& entity, BPRequiredKind kind) {
    switch (kind) {
    case BP_REQ_MESH: entity.hasMesh = true; g.scene.syncBodyShape(entity); break;
    case BP_REQ_RIGID_BODY: entity.hasPhysics = true; g.scene.syncBodyShape(entity); break;
    case BP_REQ_TRIGGER: entity.hasTrigger = true; entity.collision = 1; g.scene.syncBodyShape(entity); break;
    case BP_REQ_LIGHT: entity.isLight = true; break;
    case BP_REQ_CAMERA: entity.isCamera = true; break;
    case BP_REQ_AUDIO: entity.hasAudio = true; break;
    case BP_REQ_REVERB: entity.hasReverb = true; break;
    case BP_REQ_AI_AGENT:
        entity.hasAIAgent = true;
        if (entity.body) entity.aiDestination = entity.body->position;
        break;
    case BP_REQ_NAV_OCCLUDER: entity.hasNavigationOccluder = true; break;
    case BP_REQ_ANIMATOR: entity.hasAnimator = true; break;
    default: break;
    }
}

static bool addBlueprintComponentRecursive(Entity& entity, const std::string& relativePath,
                                           std::unordered_set<std::string>& active) {
    if (relativePath.empty()) return false;
    std::string resolved = relativePath;
    std::string resolvedAsset;
    if (bpResolveBlueprintAssetPath(g.projectDir, relativePath, resolvedAsset)) resolved = resolvedAsset;
    std::string key = resolved;
    std::replace(key.begin(), key.end(), '/', '\\');
    std::transform(key.begin(), key.end(), key.begin(), [](unsigned char c) { return (char)tolower(c); });
    if (!active.insert(key).second) {
        addLog(2, "Cyclic Blueprint dependency ignored: %s.", resolved.c_str());
        return false;
    }

    BPGraph asset;
    bool loaded = bpLoadResolvedGraph(g.projectDir, resolved, asset);
    bool attach = !(loaded && asset.uniquePerObject && entityHasExactBlueprint(entity, resolved));
    if (attach) {
        if (!entity.graphPath[0]) {
            snprintf(entity.graphPath, sizeof(entity.graphPath), "%s", resolved.c_str());
            entity.varOverrides.clear();
            entity.varAlphaOverrides.clear();
        } else {
            BlueprintComponentDef component;
            component.graphPath = resolved;
            entity.additionalBlueprints.push_back(std::move(component));
        }
    }

    if (loaded) {
        for (const BPRequiredComponent& required : asset.requiredComponents) {
            if (required.kind == BP_REQ_BLUEPRINT)
                addBlueprintComponentRecursive(entity, required.blueprintAsset, active);
            else
                applyRequiredNativeComponent(entity, required.kind);
        }
    }
    active.erase(key);
    return attach;
}

static bool addBlueprintComponent(Entity& entity, const std::string& relativePath) {
    std::unordered_set<std::string> active;
    return addBlueprintComponentRecursive(entity, relativePath, active);
}

static void ensureBlueprintRequirements(Entity& entity) {
    std::vector<std::string> attached;
    int initialCount = entityBlueprintCount(entity);
    attached.reserve(initialCount);
    for (int i = 0; i < initialCount; i++) attached.push_back(entityBlueprintPath(entity, i));
    for (const std::string& path : attached) {
        BPGraph asset;
        if (!bpLoadResolvedGraph(g.projectDir, path, asset)) continue;
        for (const BPRequiredComponent& required : asset.requiredComponents) {
            if (required.kind == BP_REQ_BLUEPRINT) {
                std::unordered_set<std::string> active;
                std::string ownerKey = path;
                std::replace(ownerKey.begin(), ownerKey.end(), '/', '\\');
                std::transform(ownerKey.begin(), ownerKey.end(), ownerKey.begin(),
                               [](unsigned char c) { return (char)tolower(c); });
                active.insert(ownerKey);
                addBlueprintComponentRecursive(entity, required.blueprintAsset, active);
            } else {
                applyRequiredNativeComponent(entity, required.kind);
            }
        }
    }
}

static void removeBlueprintComponent(Entity& entity, int componentIndex) {
    if (componentIndex < 0 || componentIndex >= entityBlueprintCount(entity)) return;
    if (componentIndex > 0) {
        entity.additionalBlueprints.erase(entity.additionalBlueprints.begin() + componentIndex - 1);
        return;
    }
    if (entity.additionalBlueprints.empty()) {
        entity.graphPath[0] = 0;
        entity.varOverrides.clear();
        entity.varAlphaOverrides.clear();
        return;
    }
    BlueprintComponentDef promoted = std::move(entity.additionalBlueprints.front());
    entity.additionalBlueprints.erase(entity.additionalBlueprints.begin());
    snprintf(entity.graphPath, sizeof(entity.graphPath), "%s", promoted.graphPath.c_str());
    entity.varOverrides = std::move(promoted.varOverrides);
    entity.varAlphaOverrides = std::move(promoted.varAlphaOverrides);
    if (promoted.collapsed) entity.detailCollapsed |= 1u << DETAIL_BLUEPRINT;
    else entity.detailCollapsed &= ~(1u << DETAIL_BLUEPRINT);
}

static void bpDestroyCb(Entity* e) {
    if (e) g.bpDestroyQueue.push_back(e->id);
}

static void bpSendMsgCb(int targetEntityId, const char* eventName);
static void bpProcessSpawnedScripts();
static std::vector<BPValue> bpCallInterfaceMessageCb(int targetEntityId,const char* interfaceAsset,
                                                     const char* functionName,const std::vector<BPValue>& args);
static BPValue bpGetBlueprintMemberCb(int targetEntityId,const char* classAsset,const char* memberName);
static bool bpSetBlueprintMemberCb(int targetEntityId,const char* classAsset,const char* memberName,const BPValue& value);
static std::vector<BPValue> bpCallBlueprintMemberCb(int targetEntityId,const char* classAsset,
                                                    const char* functionName,const std::vector<BPValue>& args);
static void bpFireBlueprintMemberEventCb(int targetEntityId,const char* classAsset,
                                         const char* eventName,const std::vector<BPValue>& args);
static void bpInvokeInspectorEventCb(Entity* target,const char* eventName);
static void bpPlayAudioCb(Entity* e) { if (e) playAudioSource(*e); }
static void bpStopAudioCb(Entity* e) {
    if (!e) return;
    g.audio.stop(e->id);
    g.audioFades.erase(e->id);
}
static void bpSetAudioVolumeCb(Entity* e, float volume) { if (e) setAudioSourceVolume(*e, volume); }
static void bpSetAudioClipCb(Entity* e, const char* clip) { if (e) setAudioSourceClip(*e, clip); }
static void bpFadeInAudioCb(Entity* e, float duration) { if (e) fadeInAudioSource(*e, duration); }
static void bpFadeOutAudioCb(Entity* e, float duration) { if (e) fadeOutAudioSource(*e, duration); }
static void bpSetAnimatorParameterCb(Entity* e,const char* name,int type,float value){
    if(!e||!e->hasAnimator||!name||!name[0])return;
    e->animatorRuntimeParameters[name]=type==ANIM_PARAM_BOOL?(value!=0?1.0f:0.0f):type==ANIM_PARAM_TRIGGER?1.0f:value;
}
static void bpBindAnimationTriggerCb(Entity* listener,Entity* animator,const char* trigger,const char* customEvent){
    if(!listener||!animator||!animator->hasAnimator||!trigger||!trigger[0]||!customEvent||!customEvent[0])return;
    bool listenerEventFound=false;
    for(const auto& live:g.bpScripts)if(live.entityId==listener->id&&live.inst.graph){
        BPEventDef* signature=live.inst.graph->findEvent(customEvent);
        if(!signature||!signature->params.empty()){addLog(2,"Animation Trigger: the Custom Event must exist and take no parameters.");return;}
        listenerEventFound=true;
        break;
    }
    if(!listenerEventFound){addLog(2,"Animation Trigger: Blueprint listener inactive or Custom Event missing.");return;}
    for(const App::AnimationTriggerBinding& binding:g.animationTriggerBindings)
        if(binding.animatorId==animator->id&&binding.listenerId==listener->id&&binding.trigger==trigger&&binding.customEvent==customEvent)return;
    g.animationTriggerBindings.push_back({animator->id,listener->id,trigger,customEvent});
}
static void bpAISetTargetCb(Entity* e, int target) { if (e) aiSetTarget(*e, target); }
static void bpAISetDestinationCb(Entity* e, const Vec3& destination) { if (e) aiSetDestination(*e, destination); }
static void bpAISetSpeedCb(Entity* e, float speed) { if (e) e->aiSpeed = (std::max)(0.0f, speed); }
static void bpAISetStoppedCb(Entity* e, bool stopped) {
    if (!e) return;
    e->aiStopped = stopped;
    if (stopped && e->body) e->body->velocity.x = e->body->velocity.z = 0;
}
static float bpAIRemainingCb(Entity* e) { return e ? e->aiRemainingDistance : 0.0f; }
static bool bpAIHasPathCb(Entity* e) { return e && e->aiHasPath; }

static Vec3 bpComponentMul(const Vec3& a, const Vec3& b) {
    return { a.x * b.x, a.y * b.y, a.z * b.z };
}

static int bpSpawnPrefabCb(const char* relativePath, const BPValue& transform,
                           const std::vector<BPValue>& exposedValues) {
    if (!relativePath || !relativePath[0]) return 0;
    std::string data;
    if (!readFile(g.projectDir + "\\" + relativePath, data)) {
        addLog(2, "Spawn Prefab: missing asset '%s'.", relativePath);
        return 0;
    }
    const size_t firstNewJoint = g.scene.joints.size();
    std::vector<int> ids = g.scene.instantiateFrom(data, {}, false);
    if (ids.empty()) {
        addLog(2, "Spawn Prefab: invalid prefab '%s'.", relativePath);
        return 0;
    }

    Entity* root = nullptr;
    for (int id : ids) {
        Entity* candidate = g.scene.byId(id);
        if (candidate && candidate->parentId == 0) { root = candidate; break; }
    }
    if (!root) return 0;
    const int rootId = root->id;
    const Vec3 oldPos = root->body->position;
    const Quat oldRot = root->body->quat;
    const Vec3 oldScale = root->scale;
    BPValue desired = transform.kind == PIN_TRANSFORM
                        ? transform : BPValue::T(transform.asVec(), {}, { 1, 1, 1 });
    Vec3 desiredScale = desired.scl;
    if (fabsf(desiredScale.x) < 1e-6f && fabsf(desiredScale.y) < 1e-6f && fabsf(desiredScale.z) < 1e-6f)
        desiredScale = { 1, 1, 1 };
    Vec3 ratio = {
        fabsf(oldScale.x) > 1e-6f ? desiredScale.x / oldScale.x : 1,
        fabsf(oldScale.y) > 1e-6f ? desiredScale.y / oldScale.y : 1,
        fabsf(oldScale.z) > 1e-6f ? desiredScale.z / oldScale.z : 1,
    };
    Quat desiredRot = Quat::fromEulerDeg(desired.rot.x, desired.rot.y, desired.rot.z).normalized();
    Quat deltaRot = (desiredRot * oldRot.conjugate()).normalized();
    for (int id : ids) {
        Entity* spawned = g.scene.byId(id);
        if (!spawned) continue;
        Vec3 relative = bpComponentMul(spawned->body->position - oldPos, ratio);
        spawned->body->position = desired.vec + deltaRot.rotate(relative);
        spawned->body->quat = (deltaRot * spawned->body->quat).normalized();
        spawned->scale = bpComponentMul(spawned->scale, ratio);
        g.scene.syncBodyShape(*spawned);
    }
    float jointScale = (fabsf(ratio.x) + fabsf(ratio.y) + fabsf(ratio.z)) / 3.0f;
    for (size_t i = firstNewJoint; i < g.scene.joints.size(); i++) g.scene.joints[i].len *= jointScale;
    if (firstNewJoint < g.scene.joints.size()) g.scene.rebuildConstraints();
    for (int id : ids) {
        Entity* spawned = g.scene.byId(id);
        if (!spawned) continue;
        Entity* parent = spawned->parentId ? g.scene.byId(spawned->parentId) : nullptr;
        if (parent) {
            Vec3 raw = parent->body->quat.conjugate().rotate(spawned->body->position - parent->body->position);
            spawned->attachPos = { fabsf(parent->scale.x)>.000001f?raw.x/parent->scale.x:raw.x,
                                   fabsf(parent->scale.y)>.000001f?raw.y/parent->scale.y:raw.y,
                                   fabsf(parent->scale.z)>.000001f?raw.z/parent->scale.z:raw.z };
            spawned->attachRot = parent->body->quat.conjugate() * spawned->body->quat;
            spawned->attachScale = { fabsf(parent->scale.x)>.000001f?spawned->scale.x/parent->scale.x:spawned->scale.x,
                                     fabsf(parent->scale.y)>.000001f?spawned->scale.y/parent->scale.y:spawned->scale.y,
                                     fabsf(parent->scale.z)>.000001f?spawned->scale.z/parent->scale.z:spawned->scale.z };
        }
        if (spawned->hasAudio && spawned->audioPlayOnAwake) playAudioSource(*spawned);
    }

    // Only the root Blueprint contributes exposed pins. Child Blueprints still
    // start normally, but never leak their variables into this Spawn node.
    root = g.scene.byId(rootId);
    if (root && root->graphPath[0]) {
        std::string graphData;
        BPGraph rootGraph;
        if (readFile(g.projectDir + "\\" + root->graphPath, graphData) && rootGraph.deserialize(graphData)) {
            size_t valueIndex = 0;
            for (const BPVarDef& var : rootGraph.vars) {
                if (!var.exposeOnSpawn || var.scope != VS_PUBLIC || var.container != VC_SINGLE) continue;
                if (valueIndex >= exposedValues.size()) break;
                g.bpSpawnOverrides[rootId][var.name] = exposedValues[valueIndex++];
            }
        }
    }
    for (int id : ids) {
        Entity* spawned = g.scene.byId(id);
        if (spawned && entityBlueprintCount(*spawned) > 0) g.bpSpawnScriptQueue.push_back(id);
    }
    addLog(1, "Spawn Prefab: '%s' created (entity #%d).", relativePath, rootId);
    return rootId;
}

// Test probes for Blueprint audio actions; kept as plain callbacks because BPContext
// deliberately uses C-style function pointers and has no editor/runtime ownership.
static int bpAudioTestMask = 0;
static float bpAudioTestVolume = 0, bpAudioTestFadeIn = 0, bpAudioTestFadeOut = 0;
static std::string bpAudioTestClip;
static void bpAudioTestSetVolume(Entity*, float value) { bpAudioTestMask |= 1; bpAudioTestVolume = value; }
static void bpAudioTestSetClip(Entity*, const char* value) { bpAudioTestMask |= 2; bpAudioTestClip = value ? value : ""; }
static void bpAudioTestFadeInCb(Entity*, float value) { bpAudioTestMask |= 4; bpAudioTestFadeIn = value; }
static void bpAudioTestFadeOutCb(Entity*, float value) { bpAudioTestMask |= 8; bpAudioTestFadeOut = value; }
static int bpSpawnTestCalls = 0;
static int bpSpawnTestCb(const char*, const BPValue&, const std::vector<BPValue>& values) {
    bpSpawnTestCalls++;
    return values.empty() ? 77 : 0;
}

static void bpDebugTraceCb(const Vec3& start, const Vec3& end, float radius, bool hit, const Vec3& hitPoint) {
    if (g.debugSegs.size() > 500) return;
    const Vec3 green = { 0.3f, 0.95f, 0.4f }, red = { 0.95f, 0.3f, 0.3f };
    const float life = 0.6f;   // "For Duration" ~0.6s
    if (hit) {
        g.debugSegs.push_back({ start, hitPoint, red, life });
        g.debugSegs.push_back({ hitPoint, end, green, life });   // remaining segment
        float s = radius > 0.02f ? radius : 0.12f;               // marker size = raggio
        g.debugSegs.push_back({ hitPoint - Vec3{ s, 0, 0 }, hitPoint + Vec3{ s, 0, 0 }, red, life });
        g.debugSegs.push_back({ hitPoint - Vec3{ 0, s, 0 }, hitPoint + Vec3{ 0, s, 0 }, red, life });
        g.debugSegs.push_back({ hitPoint - Vec3{ 0, 0, s }, hitPoint + Vec3{ 0, 0, s }, red, life });
    } else {
        g.debugSegs.push_back({ start, end, green, life });
    }
}

static float bpEvalCurveCb(const char* relativePath, float time) {
    if (!relativePath || !relativePath[0]) return 0.0f;
    // An open editor is authoritative, so Play can preview unsaved curve tweaks.
    for (const auto& ed : g.curveDocs)
        if (ed && ed->curPath == relativePath) return ed->curve.evaluate(time);
    auto it = g.curveCache.find(relativePath);
    if (it == g.curveCache.end()) {
        std::string data;
        CurveAsset asset;
        if (!readFile(g.projectDir + "\\" + relativePath, data) || !asset.deserialize(data)) return 0.0f;
        it = g.curveCache.emplace(relativePath, std::move(asset)).first;
    }
    return it->second.evaluate(time);
}

static std::string saveGameSlotPath(const char* requested) {
    std::string slot = requested && requested[0] ? requested : "Default";
    for (char& c : slot) if (!isalnum((unsigned char)c) && c != '_' && c != '-') c = '_';
    if (slot.size() > 64) slot.resize(64);
    return (fs::path(g.projectDir) / "Saved" / "SaveGames" / (slot + ".sav")).string();
}

static App::LiveScript* bpLiveObject(int objectId) {
    if (objectId <= 0) objectId = g.gameInstanceEntity;
    for (auto& live : g.bpScripts) if (!live.inst.dead && live.entityId == objectId) return &live;
    return nullptr;
}

static bool bpSaveGameSlotCb(int objectId, const char* slot) {
    App::LiveScript* live = bpLiveObject(objectId);
    if (!live) { addLog(2, "SaveGame: invalid Blueprint object."); return false; }
    std::ostringstream output;
    output << "IMPULSOSAVE 1\n";
    for (const auto& pair : live->inst.vars) {
        const BPValue& value = pair.second.single;
        output << "value " << std::quoted(pair.first) << " " << (int)value.kind << " "
               << value.num << " " << (value.b ? 1 : 0) << " " << value.ent << " "
               << value.vec.x << " " << value.vec.y << " " << value.vec.z << " "
               << value.rot.x << " " << value.rot.y << " " << value.rot.z << " "
               << value.scl.x << " " << value.scl.y << " " << value.scl.z << " "
               << std::quoted(value.str) << "\n";
    }
    std::string path = saveGameSlotPath(slot);
    std::error_code ec;
    fs::create_directories(fs::path(path).parent_path(), ec);
    bool ok = !ec && writeFile(path, output.str());
    addLog(ok ? 1 : 2, ok ? "SaveGame written to slot '%s'." : "SaveGame: writing slot '%s' failed.", slot ? slot : "Default");
    return ok;
}

static bool bpLoadGameSlotCb(int objectId, const char* slot) {
    App::LiveScript* live = bpLiveObject(objectId);
    std::string data;
    if (!live || !readFile(saveGameSlotPath(slot), data)) { addLog(2, "SaveGame: slot '%s' not found.", slot ? slot : "Default"); return false; }
    std::istringstream input(data);
    std::string header;
    if (!std::getline(input, header) || header.rfind("IMPULSOSAVE ", 0) != 0) return false;
    std::string record;
    int loaded = 0;
    while (input >> record) {
        if (record != "value") { std::getline(input, record); continue; }
        std::string name, text;
        int kind = 0, boolean = 0;
        BPValue value;
        if (!(input >> std::quoted(name) >> kind >> value.num >> boolean >> value.ent
                    >> value.vec.x >> value.vec.y >> value.vec.z
                    >> value.rot.x >> value.rot.y >> value.rot.z
                    >> value.scl.x >> value.scl.y >> value.scl.z >> std::quoted(text))) break;
        auto target = live->inst.vars.find(name);
        if (target == live->inst.vars.end()) continue;
        value.kind = (PinKind)kind;
        value.b = boolean != 0;
        value.str = text;
        target->second.single = value;
        loaded++;
    }
    addLog(1, "SaveGame loaded from slot '%s': %d variables.", slot ? slot : "Default", loaded);
    return true;
}

static bool bpSaveGameExistsCb(const char* slot) {
    std::error_code ec;
    return fs::is_regular_file(saveGameSlotPath(slot), ec);
}

static int bpCreateSaveGameCb(const char* classPath) {
    if (!classPath || !classPath[0]) return 0;
    BPGraph graph;
    if (!bpLoadResolvedGraph(g.projectDir, classPath, graph) || graph.classKind != BP_CLASS_SAVEGAME) {
        addLog(2, "Create Save Game: invalid class: %s", classPath);
        return 0;
    }
    Entity& object = g.scene.spawnEmpty("SaveGame (Runtime)");
    snprintf(object.graphPath, sizeof(object.graphPath), "%s", classPath);
    object.tags.push_back("SaveGame");
    g.bpSpawnScriptQueue.push_back(object.id);
    return object.id;
}

// Open Level defers the actual scene swap: loading a new .imp while blueprints
// iterate the current scene would be unsafe, so we only record the request and
// process it after the simulation step completes (see processPendingOpenLevel).
static void bpOpenLevelCb(const char* levelName) {
    if (levelName && levelName[0]) g.pendingOpenLevel = levelName;
}

// ── Blueprint UI widgets (Create Widget / Add to Viewport / Set Text|Value) ──
static RuntimeWidget* runtimeWidgetByHandle(int handle) {
    if (handle <= 0) return nullptr;
    for (auto& rw : g.runtimeWidgets) if (rw->handle == handle) return rw.get();
    return nullptr;
}

static BPContext makeBPCtx(Entity* e, float impulse, int key, int other);
static bool bpBindDispatcherCb(int targetEntity, int targetWidget, const char* dispatcher,
                               int listenerEntity, int listenerWidget, const char* eventName);
static void bpFireDispatcherEventCb(int listenerEntity, int listenerWidget, const char* eventName,
                                    const std::vector<BPValue>& args);
static BPValue bpGetWidgetMemberCb(int handle, const char* memberName);
static bool bpSetWidgetMemberCb(int handle, const char* memberName, const BPValue& value);
static std::vector<BPValue> bpCallWidgetMemberCb(int handle, const char* functionName,
                                                 const std::vector<BPValue>& args);
static void bpFireWidgetMemberEventCb(int handle, const char* eventName,
                                      const std::vector<BPValue>& args);

// Context for a widget's own graph: no owning entity, but `selfWidget` set so
// the widget nodes can leave their Widget pin unwired.
static BPContext makeWidgetCtx(const RuntimeWidget& rw, const char* element = "") {
    BPContext ctx = makeBPCtx(nullptr, 0, -1, 0);
    ctx.selfWidget = rw.handle;
    ctx.eventWidgetElement = element;
    return ctx;
}

// Fire one event on a widget instance. Re-fetches the widget afterwards because
// the graph may have created or destroyed widgets while running.
static void widgetFire(int handle, int evType, const char* element = "") {
    RuntimeWidget* rw = runtimeWidgetByHandle(handle);
    if (!rw || !rw->inst.graph || rw->inst.dead) return;
    BPContext ctx = makeWidgetCtx(*rw, element);
    rw->inst.fire(evType, ctx);
}

// Instantiate a .wgt: its own copy of the tree plus a live graph instance.
// Fires On Initialized and Pre Construct, like UMG's CreateWidget.
static int createRuntimeWidget(const char* assetPath) {
    if (!assetPath || !assetPath[0]) { addLog(2, "Create Widget: no .wgt asset selected."); return 0; }
    auto rw = std::make_unique<RuntimeWidget>();
    rw->handle = g.nextWidgetHandle++;
    rw->assetPath = assetPath;
    std::string data;
    if (!readFile(g.projectDir + "\\" + assetPath, data)) {
        addLog(2, "Create Widget: widget not found: %s", assetPath);
        return 0;
    }
    // one graph per asset (instance state lives in BPInstance::vars, as for actors)
    auto cached = g.widgetGraphCache.find(assetPath);
    if (cached == g.widgetGraphCache.end())
        cached = g.widgetGraphCache.emplace(assetPath, BPGraph{}).first;
    if (!widgetParseAsset(data, rw->asset, cached->second)) {
        addLog(2, "Create Widget: invalid widget: %s", assetPath);
        return 0;
    }
    rw->inst.graph = &cached->second;
    rw->inst.initVars(nullptr);
    int handle = rw->handle;
    g.runtimeWidgets.push_back(std::move(rw));
    widgetFire(handle, BP_EV_W_INITIALIZED);
    widgetFire(handle, BP_EV_W_PRECONSTRUCT);
    return handle;
}
static int bpCreateWidgetCb(const char* assetPath) { return createRuntimeWidget(assetPath); }
static void bpAddWidgetViewportCb(int handle) {
    RuntimeWidget* rw = runtimeWidgetByHandle(handle);
    if (!rw || rw->visible) return;
    rw->visible = true;
    rw->constructed = true;
    widgetFire(handle, BP_EV_W_CONSTRUCT);   // UMG fires Construct when it enters the viewport
}
static void bpRemoveWidgetViewportCb(int handle) {
    RuntimeWidget* rw = runtimeWidgetByHandle(handle);
    if (!rw) return;
    rw->visible = false;
    rw->constructed = false;
    rw->hoverElement.clear();
    rw->pressElement.clear();
}
static void bpSetWidgetTextCb(int handle, const char* element, const char* text) {
    RuntimeWidget* rw = runtimeWidgetByHandle(handle);
    if (!rw || !element) return;
    for (WidgetNode& node : rw->asset.nodes)
        if (_stricmp(node.name, element) == 0) snprintf(node.text, sizeof(node.text), "%s", text ? text : "");
}
static void bpSetWidgetValueCb(int handle, const char* element, float value) {
    RuntimeWidget* rw = runtimeWidgetByHandle(handle);
    if (!rw || !element) return;
    for (WidgetNode& node : rw->asset.nodes)
        if (_stricmp(node.name, element) == 0) node.value = value;
}

// ── component properties by name (Percent, Font Size, Color, Visible, ...) ──
static WidgetNode* runtimeWidgetElement(int handle, const char* element) {
    RuntimeWidget* rw = runtimeWidgetByHandle(handle);
    if (!rw || !element || !element[0]) return nullptr;
    for (WidgetNode& node : rw->asset.nodes)
        if (_stricmp(node.name, element) == 0) return &node;
    return nullptr;
}
static bool bpGetWidgetNumberCb(int handle, const char* element, const char* prop, float& out) {
    const WidgetNode* n = runtimeWidgetElement(handle, element);
    return n && widgetGetNumber(*n, prop, out);
}
static void bpSetWidgetNumberCb(int handle, const char* element, const char* prop, float value) {
    if (WidgetNode* n = runtimeWidgetElement(handle, element)) widgetSetNumber(*n, prop, value);
}
static bool bpGetWidgetStringCb(int handle, const char* element, const char* prop, std::string& out) {
    const WidgetNode* n = runtimeWidgetElement(handle, element);
    return n && widgetGetString(*n, prop, out);
}
static void bpSetWidgetStringCb(int handle, const char* element, const char* prop, const char* value) {
    if (WidgetNode* n = runtimeWidgetElement(handle, element)) widgetSetString(*n, prop, value ? value : "");
}
static bool bpGetWidgetColorCb(int handle, const char* element, const char* prop, Vec3& rgb, float& alpha) {
    const WidgetNode* n = runtimeWidgetElement(handle, element);
    return n && widgetGetColor(*n, prop, rgb, alpha);
}
static void bpSetWidgetColorCb(int handle, const char* element, const char* prop, const Vec3& rgb, float alpha) {
    if (WidgetNode* n = runtimeWidgetElement(handle, element)) widgetSetColor(*n, prop, rgb, alpha);
}
static bool bpGetWidgetBoolCb(int handle, const char* element, const char* prop, bool& out) {
    const WidgetNode* n = runtimeWidgetElement(handle, element);
    return n && widgetGetBool(*n, prop, out);
}
static void bpSetWidgetBoolCb(int handle, const char* element, const char* prop, bool value) {
    if (WidgetNode* n = runtimeWidgetElement(handle, element)) widgetSetBool(*n, prop, value);
}

// ── Event Dispatchers across Blueprints and Widgets ──
// One instance owns the dispatcher and another listens to it, so both sides
// have to be reachable by id: a widget handle addresses a RuntimeWidget, an
// entity id addresses that object's live Blueprint.
// An actor can carry SEVERAL Blueprint components, each with its own graph, so
// "the target's Blueprint" is ambiguous: taking the first one made a dispatcher
// declared on, say, a Health Component invisible. Pick the component that
// actually declares what we are after, and only fall back to the first.
static BPInstance* bpInstanceOwning(int entityId, int widgetHandle,
                                    const std::function<bool(const BPGraph&)>& declares) {
    if (widgetHandle) {
        RuntimeWidget* rw = runtimeWidgetByHandle(widgetHandle);
        if (!rw || rw->inst.dead || !rw->inst.graph) return nullptr;
        return declares(*rw->inst.graph) ? &rw->inst : nullptr;
    }
    int objectId = entityId > 0 ? entityId : g.gameInstanceEntity;
    BPInstance* first = nullptr;
    for (auto& live : g.bpScripts) {
        if (live.inst.dead || live.entityId != objectId || !live.inst.graph) continue;
        if (!first) first = &live.inst;
        if (declares(*live.inst.graph)) return &live.inst;
    }
    return first;
}

// What is actually alive on the target, so a failed bind can say where it looked
// instead of just "not found" — the usual cause is that the Blueprint declaring
// the dispatcher was never added to that actor as a component.
static std::string bpLiveComponentList(int entityId) {
    std::string out;
    Entity* e = g.scene.byId(entityId);
    for (auto& live : g.bpScripts) {
        if (live.inst.dead || live.entityId != entityId) continue;
        const char* path = e ? entityBlueprintPath(*e, live.componentIndex) : nullptr;
        if (!out.empty()) out += ", ";
        out += (path && path[0]) ? path : "(unnamed)";
    }
    return out.empty() ? std::string("none") : out;
}

static bool bpBindDispatcherCb(int targetEntity, int targetWidget, const char* dispatcher,
                               int listenerEntity, int listenerWidget, const char* eventName) {
    BPInstance* target = bpInstanceOwning(targetEntity, targetWidget, [&](const BPGraph& g2) {
        return const_cast<BPGraph&>(g2).findDispatcher(dispatcher) != nullptr;
    });
    BPInstance* listener = bpInstanceOwning(listenerEntity, listenerWidget, [&](const BPGraph& g2) {
        return const_cast<BPGraph&>(g2).findEvent(eventName) != nullptr;
    });
    if (!target || !target->graph) {
        if (targetWidget) addLog(2, "Bind Dispatcher '%s': widget %d is not alive.", dispatcher, targetWidget);
        else addLog(2, "Bind Dispatcher '%s': no Blueprint is running on target object %d. Live components: %s.",
                    dispatcher, targetEntity, bpLiveComponentList(targetEntity).c_str());
        return false;
    }
    if (!listener || !listener->graph) {
        addLog(2, "Bind Dispatcher '%s': the listener has no Custom Event named '%s'.", dispatcher, eventName);
        return false;
    }
    std::string why;
    // the binding is stored on the OWNER of the dispatcher, tagged with who listens
    if (target->bindDispatcher(dispatcher, listenerEntity, listenerWidget, eventName,
                               listener->graph, &why))
        return true;
    if (targetWidget)
        addLog(2, "Bind Dispatcher '%s': %s (widget %d).", dispatcher, why.c_str(), targetWidget);
    else
        addLog(2, "Bind Dispatcher '%s': %s Searched every Blueprint on object %d: %s.",
               dispatcher, why.c_str(), targetEntity, bpLiveComponentList(targetEntity).c_str());
    return false;
}

static void bpFireDispatcherEventCb(int listenerEntity, int listenerWidget, const char* eventName,
                                    const std::vector<BPValue>& args) {
    if (listenerWidget) {
        RuntimeWidget* rw = runtimeWidgetByHandle(listenerWidget);
        if (!rw || rw->inst.dead || !rw->inst.graph) return;
        BPContext ctx = makeBPCtx(nullptr, 0, -1, 0);
        ctx.selfWidget = rw->handle;
        rw->inst.fireCustomWithArgs(eventName, args, ctx);
        return;
    }
    // the event belongs to whichever Blueprint component declares it, which is
    // not necessarily the actor's first one
    int objectId = listenerEntity > 0 ? listenerEntity : g.gameInstanceEntity;
    for (auto& live : g.bpScripts) {
        if (live.inst.dead || live.entityId != objectId || !live.inst.graph) continue;
        if (!live.inst.graph->findEvent(eventName)) continue;
        Entity* owner = g.scene.byId(live.entityId);
        live.inst.entity = owner;
        BPContext ctx = makeBPCtx(owner, 0, -1, 0);
        live.inst.fireCustomWithArgs(eventName, args, ctx);
        return;
    }
}

static BPContext makeBPCtx(Entity* e, float impulse, int key, int other) {
    BPContext ctx;
    ctx.entity = e;
    ctx.scene = &g.scene;
    ctx.dt = FIXED_DT;
    ctx.time = g.playTime;
    ctx.keysDown = g.bpKeysDown;
    ctx.eventImpulse = impulse;
    ctx.eventKey = key;
    ctx.eventOther = other;
    ctx.gameModeEntity = g.gameModeEntity;
    ctx.gameInstanceEntity = g.gameInstanceEntity;
    ctx.playerControllerEntity = g.playerControllerEntity;
    ctx.playerPawnEntity = g.playerPawnEntity;
    ctx.axisValues = g.bpAxisValues;
    ctx.log = addLog;
    ctx.printString = bpPrintStringCb;
    ctx.requestDestroy = bpDestroyCb;
    ctx.sendMessage = bpSendMsgCb;
    ctx.drawDebugTrace = bpDebugTraceCb;
    ctx.evalCurve = bpEvalCurveCb;
    ctx.playAudio = bpPlayAudioCb;
    ctx.stopAudio = bpStopAudioCb;
    ctx.setAudioVolume = bpSetAudioVolumeCb;
    ctx.setAudioClip = bpSetAudioClipCb;
    ctx.fadeInAudio = bpFadeInAudioCb;
    ctx.fadeOutAudio = bpFadeOutAudioCb;
    ctx.spawnPrefab = bpSpawnPrefabCb;
    ctx.aiSetTarget = bpAISetTargetCb;
    ctx.aiSetDestination = bpAISetDestinationCb;
    ctx.aiSetSpeed = bpAISetSpeedCb;
    ctx.aiSetStopped = bpAISetStoppedCb;
    ctx.aiRemainingDistance = bpAIRemainingCb;
    ctx.aiHasPath = bpAIHasPathCb;
    ctx.saveGameSlot = bpSaveGameSlotCb;
    ctx.loadGameSlot = bpLoadGameSlotCb;
    ctx.saveGameExists = bpSaveGameExistsCb;
    ctx.createSaveGame = bpCreateSaveGameCb;
    ctx.openLevel = bpOpenLevelCb;
    g.currentLevelName = fs::path(g.projectPath).stem().string();
    ctx.currentLevelName = g.currentLevelName.c_str();
    ctx.createWidget = bpCreateWidgetCb;
    ctx.addWidgetToViewport = bpAddWidgetViewportCb;
    ctx.removeWidgetFromViewport = bpRemoveWidgetViewportCb;
    ctx.setWidgetText = bpSetWidgetTextCb;
    ctx.setWidgetValue = bpSetWidgetValueCb;
    ctx.getWidgetNumber = bpGetWidgetNumberCb;
    ctx.setWidgetNumber = bpSetWidgetNumberCb;
    ctx.getWidgetString = bpGetWidgetStringCb;
    ctx.setWidgetString = bpSetWidgetStringCb;
    ctx.getWidgetColor = bpGetWidgetColorCb;
    ctx.setWidgetColor = bpSetWidgetColorCb;
    ctx.getWidgetBool = bpGetWidgetBoolCb;
    ctx.setWidgetBool = bpSetWidgetBoolCb;
    ctx.setAnimatorParameter = bpSetAnimatorParameterCb;
    ctx.bindAnimationTrigger = bpBindAnimationTriggerCb;
    ctx.callInterfaceMessage = bpCallInterfaceMessageCb;
    ctx.getBlueprintMember = bpGetBlueprintMemberCb;
    ctx.setBlueprintMember = bpSetBlueprintMemberCb;
    ctx.callBlueprintMember = bpCallBlueprintMemberCb;
    ctx.fireBlueprintMemberEvent = bpFireBlueprintMemberEventCb;
    ctx.invokeInspectorEvent = bpInvokeInspectorEventCb;
    ctx.bindDispatcher = bpBindDispatcherCb;
    ctx.fireDispatcherEvent = bpFireDispatcherEventCb;
    ctx.getWidgetMember = bpGetWidgetMemberCb;
    ctx.setWidgetMember = bpSetWidgetMemberCb;
    ctx.callWidgetMember = bpCallWidgetMemberCb;
    ctx.fireWidgetMemberEvent = bpFireWidgetMemberEventCb;
    return ctx;
}

static std::vector<BPValue> bpCallInterfaceMessageCb(int targetEntityId,const char* interfaceAsset,
                                                     const char* functionName,const std::vector<BPValue>& args){
    if(targetEntityId<=0||!interfaceAsset||!interfaceAsset[0]||!functionName||!functionName[0])return {};
    auto sameAsset=[](std::string a,std::string b){for(char&c:a)if(c=='/')c='\\';for(char&c:b)if(c=='/')c='\\';return _stricmp(a.c_str(),b.c_str())==0;};
    std::string interfaceData;BPGraph interfaceGraph;
    BPFunc* signature=nullptr;
    if(readFile(g.projectDir+"\\"+interfaceAsset,interfaceData)&&interfaceGraph.deserialize(interfaceData))
        signature=interfaceGraph.findFunc(functionName);
    std::vector<BPValue> lastResult;
    for(auto& live:g.bpScripts){
        if(live.inst.dead||live.entityId!=targetEntityId||!live.inst.graph)continue;
        bool implements=false;
        for(const std::string& asset:live.inst.graph->interfaceAssets)if(sameAsset(asset,interfaceAsset)){implements=true;break;}
        if(!implements)continue; // another Blueprint component may implement it
        Entity* target=g.scene.byId(targetEntityId);if(!target)return lastResult;
        live.inst.entity=target;BPContext context=makeBPCtx(target,0,-1,0);
        if(signature&&signature->outs.empty()){
            live.inst.fireCustomWithArgs(functionName,args,context);
            continue;
        }
        lastResult=live.inst.callNamedFunction(functionName,args,context);
    }
    // Messages are broadcast to every compatible Blueprint component on the
    // Object. When the interface declares outputs, the last implementation's
    // values are exposed by the node; no implementation remains a safe no-op.
    return lastResult;
}

static bool appBlueprintPathIsA(std::string actual, std::string requested) {
    if (actual.rfind("blueprint:", 0) == 0) actual.erase(0, 10);
    if (requested.rfind("blueprint:", 0) == 0) requested.erase(0, 10);
    std::string resolvedActual;
    if (bpResolveBlueprintAssetPath(g.projectDir, actual, resolvedActual)) actual = resolvedActual;
    std::string resolvedRequested;
    if (bpResolveBlueprintAssetPath(g.projectDir, requested, resolvedRequested)) requested = resolvedRequested;
    for (int depth = 0; depth < 16 && !actual.empty(); depth++) {
        std::string a = actual, b = requested;
        for (char& c : a) if (c == '/') c = '\\';
        for (char& c : b) if (c == '/') c = '\\';
        if (_stricmp(a.c_str(), b.c_str()) == 0) return true;
        std::string data; BPGraph raw;
        if (!readFile(g.projectDir + "\\" + actual, data) || !raw.deserialize(data)) break;
        actual = raw.parentAsset;
    }
    return false;
}

static App::LiveScript* appPublicBlueprintMember(int targetEntityId, const char* classAsset,
                                                  const char* memberName, bool function) {
    Entity* target = g.scene.byId(targetEntityId);
    if (!target || !classAsset || !classAsset[0] || !memberName || !memberName[0]) return nullptr;
    for (auto& live : g.bpScripts) {
        if (live.inst.dead || live.entityId != targetEntityId || !live.inst.graph) continue;
        const char* actual = entityBlueprintPath(*target, live.componentIndex);
        if (!actual || !actual[0] || !appBlueprintPathIsA(actual, classAsset)) continue;
        if (function) {
            BPFunc* def = live.inst.graph->findFunc(memberName);
            if (def && def->scope == VS_PUBLIC) return &live;
        } else {
            BPVarDef* def = live.inst.graph->findVar(memberName);
            if (def && def->scope == VS_PUBLIC && def->container == VC_SINGLE) return &live;
        }
    }
    return nullptr;
}

static BPValue bpGetBlueprintMemberCb(int targetEntityId,const char* classAsset,const char* memberName) {
    App::LiveScript* live = appPublicBlueprintMember(targetEntityId, classAsset, memberName, false);
    if (!live) return {};
    auto value = live->inst.vars.find(memberName);
    return value != live->inst.vars.end() ? value->second.single : BPValue{};
}

static bool bpSetBlueprintMemberCb(int targetEntityId,const char* classAsset,const char* memberName,const BPValue& value) {
    App::LiveScript* live = appPublicBlueprintMember(targetEntityId, classAsset, memberName, false);
    if (!live) return false;
    auto target = live->inst.vars.find(memberName);
    if (target == live->inst.vars.end()) return false;
    target->second.single = value;
    return true;
}

static std::vector<BPValue> bpCallBlueprintMemberCb(int targetEntityId,const char* classAsset,
                                                    const char* functionName,const std::vector<BPValue>& args) {
    App::LiveScript* live = appPublicBlueprintMember(targetEntityId, classAsset, functionName, true);
    Entity* target = g.scene.byId(targetEntityId);
    if (!live || !target) return {};
    live->inst.entity = target;
    BPContext context = makeBPCtx(target, 0, -1, 0);
    return live->inst.callNamedFunction(functionName, args, context);
}

static void bpFireBlueprintMemberEventCb(int targetEntityId,const char* classAsset,
                                         const char* eventName,const std::vector<BPValue>& args) {
    Entity* target = g.scene.byId(targetEntityId);
    if (!target || !eventName || !eventName[0]) return;
    for (auto& live : g.bpScripts) {
        if (live.inst.dead || live.entityId != targetEntityId || !live.inst.graph) continue;
        const char* actual = entityBlueprintPath(*target, live.componentIndex);
        if (!actual || !actual[0] || !appBlueprintPathIsA(actual, classAsset)) continue;
        BPEventDef* event = live.inst.graph->findEvent(eventName);
        if (!event || event->scope != VS_PUBLIC) continue;
        live.inst.entity = target;
        BPContext context = makeBPCtx(target, 0, -1, 0);
        live.inst.fireCustomWithArgs(eventName, args, context);
        return;
    }
}

// ── members of a Widget reached through a variable ──
// The public/private split is the same as for Blueprint members: only PUBLIC
// members of the widget's graph are reachable from outside it.
static BPInstance* bpPublicWidgetMember(int handle, const char* memberName, bool asFunction) {
    RuntimeWidget* rw = runtimeWidgetByHandle(handle);
    if (!rw || rw->inst.dead || !rw->inst.graph || !memberName || !memberName[0]) return nullptr;
    if (asFunction) {
        BPFunc* fn = rw->inst.graph->findFunc(memberName);
        return fn && fn->scope == VS_PUBLIC ? &rw->inst : nullptr;
    }
    BPVarDef* var = rw->inst.graph->findVar(memberName);
    return var && var->scope == VS_PUBLIC && var->container == VC_SINGLE ? &rw->inst : nullptr;
}

static BPValue bpGetWidgetMemberCb(int handle, const char* memberName) {
    BPInstance* inst = bpPublicWidgetMember(handle, memberName, false);
    if (!inst) return {};
    auto value = inst->vars.find(memberName);
    return value != inst->vars.end() ? value->second.single : BPValue{};
}

static bool bpSetWidgetMemberCb(int handle, const char* memberName, const BPValue& value) {
    BPInstance* inst = bpPublicWidgetMember(handle, memberName, false);
    if (!inst) return false;
    auto target = inst->vars.find(memberName);
    if (target == inst->vars.end()) return false;
    target->second.single = value;
    return true;
}

static std::vector<BPValue> bpCallWidgetMemberCb(int handle, const char* functionName,
                                                 const std::vector<BPValue>& args) {
    BPInstance* inst = bpPublicWidgetMember(handle, functionName, true);
    if (!inst) return {};
    BPContext context = makeBPCtx(nullptr, 0, -1, 0);
    context.selfWidget = handle;              // the widget's own nodes still mean "this one"
    return inst->callNamedFunction(functionName, args, context);
}

static void bpFireWidgetMemberEventCb(int handle, const char* eventName,
                                      const std::vector<BPValue>& args) {
    RuntimeWidget* rw = runtimeWidgetByHandle(handle);
    if (!rw || rw->inst.dead || !rw->inst.graph || !eventName || !eventName[0]) return;
    BPEventDef* event = rw->inst.graph->findEvent(eventName);
    if (!event || event->scope != VS_PUBLIC) return;
    BPContext context = makeBPCtx(nullptr, 0, -1, 0);
    context.selfWidget = handle;
    rw->inst.fireCustomWithArgs(eventName, args, context);
}

static BPValue inspectorArgumentValue(const InspectorEventArgument& argument){
    PinKind kind=(PinKind)argument.kind;
    switch(kind){
    case PIN_BOOL:return BPValue::B(argument.value.x!=0);
    case PIN_INT:return BPValue::N((float)(int)argument.value.x);
    case PIN_VEC:return BPValue::V(argument.value);
    case PIN_VEC2:{BPValue value=BPValue::V(argument.value);value.kind=PIN_VEC2;return value;}
    case PIN_STR:return BPValue::S(argument.text);
    case PIN_ENT:return BPValue::E(argument.objectId);
    case PIN_TRANSFORM:{Entity* object=g.scene.byId(argument.objectId);return object&&object->body?
        BPValue::T(object->body->position,quatToEulerDeg(object->body->quat),object->scale):BPValue::T({},{},{1,1,1});}
    case PIN_COLOR:return BPValue::C(argument.value,argument.alpha);
    case PIN_TIMER_HANDLE:return BPValue::H((int)argument.value.x);
    default:return BPValue::N(argument.value.x);
    }
}

static void bpInvokeInspectorEventCb(Entity* target,const char* eventName){
    if(!target||!eventName||!eventName[0]||!target->hasInspectorEvents)return;
    auto event=std::find_if(target->inspectorEvents.begin(),target->inspectorEvents.end(),
        [&](const InspectorEventDef& candidate){return candidate.name==eventName;});
    if(event==target->inspectorEvents.end())return;
    // Copy first: a called Blueprint is allowed to mutate scene/component data.
    std::vector<InspectorEventListener> listeners=event->listeners;
    for(const InspectorEventListener& listener:listeners){
        if(listener.targetEntity<=0||listener.callable.empty())continue;
        Entity* receiver=g.scene.byId(listener.targetEntity);
        if(!receiver)continue;
        std::vector<BPValue> args;for(const InspectorEventArgument& argument:listener.arguments)args.push_back(inspectorArgumentValue(argument));
        if(listener.callable[0]=='@'){
            if(listener.callable=="@Transform.SetWorldLocation"&&!args.empty()&&receiver->body){Vec3 old=receiver->body->position;receiver->body->position=args[0].asVec();receiver->body->updateAABB();g.scene.moveDescendants(receiver->id,receiver->body->position-old);}
            else if(listener.callable=="@Transform.SetWorldRotation"&&!args.empty()&&receiver->body){Quat old=receiver->body->quat,next=Quat::fromEulerDeg(args[0].vec.x,args[0].vec.y,args[0].vec.z);g.scene.rotateDescendants(receiver->id,receiver->body->position,old,next);receiver->body->quat=next;receiver->body->updateAABB();}
            else if(listener.callable=="@MeshRenderer.SetColor"&&!args.empty()){receiver->color=args[0].vec;receiver->colorAlpha=args[0].alpha;}
            else if(listener.callable=="@AudioSource.Play")bpPlayAudioCb(receiver);
            else if(listener.callable=="@AudioSource.Stop")bpStopAudioCb(receiver);
            else if(listener.callable=="@AudioSource.SetVolume"&&!args.empty())bpSetAudioVolumeCb(receiver,args[0].asNum());
            else if(listener.callable=="@AIAgent.SetStopped"&&!args.empty())bpAISetStoppedCb(receiver,args[0].asBool());
            continue;
        }
        for(App::LiveScript& live:g.bpScripts){
            if(live.inst.dead||live.entityId!=listener.targetEntity||!live.inst.graph)continue;
            bool ownsCallable=listener.customEvent?live.inst.graph->findEvent(listener.callable.c_str())!=nullptr
                                                  :live.inst.graph->findFunc(listener.callable.c_str())!=nullptr;
            if(!ownsCallable)continue;
            live.inst.entity=receiver;BPContext context=makeBPCtx(receiver,0,-1,0);
            if(listener.customEvent)live.inst.fireCustomWithArgs(listener.callable.c_str(),args,context);
            else live.inst.callNamedFunction(listener.callable.c_str(),args,context);
            break;
        }
    }
    bpProcessSpawnedScripts();
}

static int bpAddLiveScripts(int entityId) {
    Entity* e = g.scene.byId(entityId);
    if (!e) return 0;
    int added = 0;
    for (int componentIndex = 0; componentIndex < entityBlueprintCount(*e); componentIndex++) {
        std::string path = entityBlueprintPath(*e, componentIndex);
        if (path.empty()) continue;
        std::string resolvedPath;
        if (bpResolveBlueprintAssetPath(g.projectDir, path, resolvedPath) &&
            _stricmp(path.c_str(), resolvedPath.c_str()) != 0) {
            setEntityBlueprintPath(*e, componentIndex, resolvedPath);
            path = resolvedPath;
        }
        bool alreadyLive = false;
        for (const auto& live : g.bpScripts) {
            if (!live.inst.dead && live.entityId == entityId && live.componentIndex == componentIndex) {
                alreadyLive = true;
                break;
            }
        }
        if (alreadyLive) continue;
        auto graph = g.graphCache.find(path);
        if (graph == g.graphCache.end()) {
            BPGraph loaded;
            if (!bpLoadResolvedGraph(g.projectDir, path, loaded)) {
                addLog(2, "Blueprint missing, invalid or cyclic parent: %s", path.c_str());
                continue;
            }
            graph = g.graphCache.emplace(path, std::move(loaded)).first;
        }
        for (const std::string& tag : graph->second.defaultTags)
            if (!tag.empty() && std::find(e->tags.begin(), e->tags.end(), tag) == e->tags.end()) e->tags.push_back(tag);
        std::map<std::string, Vec3>* valueOverrides = entityBlueprintOverrides(*e, componentIndex);
        std::map<std::string, float>* alphaOverrides = entityBlueprintAlphaOverrides(*e, componentIndex);
        App::LiveScript live;
        live.entityId = entityId;
        live.componentIndex = componentIndex;
        live.inst.graph = &graph->second;
        live.inst.initVars(valueOverrides, alphaOverrides);
        if (entityId == g.gameInstanceEntity && componentIndex == 0 && !g.persistentGameInstanceVars.empty())
            live.inst.vars = g.persistentGameInstanceVars;
        live.inst.applyRefOverrides(valueOverrides, &g.scene);
        // Spawn-node exposed values belong to the primary Blueprint component.
        auto overrides = g.bpSpawnOverrides.find(entityId);
        if (componentIndex == 0 && overrides != g.bpSpawnOverrides.end()) {
            for (const auto& pair : overrides->second) {
                auto variable = live.inst.vars.find(pair.first);
                if (variable != live.inst.vars.end()) variable->second.single = pair.second;
            }
            g.bpSpawnOverrides.erase(overrides);
        }
        g.bpScripts.push_back(std::move(live));
        added++;
    }
    return added;
}

static void bpProcessSpawnedScripts() {
    if (g.bpProcessingSpawns) return;
    g.bpProcessingSpawns = true;
    int safety = 0;
    while (!g.bpSpawnScriptQueue.empty() && safety++ < 256) {
        std::vector<int> pending = std::move(g.bpSpawnScriptQueue);
        g.bpSpawnScriptQueue.clear();
        for (int id : pending) {
            size_t firstAdded = g.bpScripts.size();
            if (!bpAddLiveScripts(id)) continue;
            Entity* entity = g.scene.byId(id);
            if (!entity) continue;
            for (size_t scriptIndex = firstAdded; scriptIndex < g.bpScripts.size(); scriptIndex++) {
                App::LiveScript& live = g.bpScripts[scriptIndex];
                if (live.entityId != id) continue;
                live.inst.entity = entity;
                BPContext context = makeBPCtx(entity, 0, -1, 0);
                live.inst.fire(BP_EV_CONSTRUCT, context);
                if (g.bpWorldBegun) live.inst.fire(BP_EV_START, context);
            }
        }
    }
    if (safety >= 256 && !g.bpSpawnScriptQueue.empty())
        addLog(2, "Spawn Prefab aborted: possible infinite loop in the Construction Script.");
    g.bpProcessingSpawns = false;
}

static void bpFireAll(int evType, float impulse = 0, int key = -1, int onlyEntity = -1, int other = 0, int outPin = 0) {
    for (auto& ls : g.bpScripts) {
        if (ls.inst.dead) continue;
        if (onlyEntity >= 0 && ls.entityId != onlyEntity) continue;
        Entity* e = g.scene.byId(ls.entityId);
        if (!e) { ls.inst.dead = true; continue; }
        ls.inst.entity = e;
        BPContext ctx = makeBPCtx(e, impulse, key, other);
        ls.inst.fire(evType, ctx, outPin);
    }
    bpProcessSpawnedScripts();
}

static void bpFireOverlapBindings(bool begin, int componentId, int otherActorId) {
    for (auto& live : g.bpScripts) {
        if (live.inst.dead) continue;
        Entity* owner = g.scene.byId(live.entityId);
        if (!owner) { live.inst.dead = true; continue; }
        live.inst.entity = owner;
        BPContext context = makeBPCtx(owner, 0, -1, otherActorId);
        live.inst.fireOverlapBinding(begin, componentId, otherActorId, context);
    }
    bpProcessSpawnedScripts();
}

static void bpFireAnimationTrigger(int animatorId,const std::string& trigger) {
    std::vector<App::AnimationTriggerBinding> bindings=g.animationTriggerBindings;
    for(const App::AnimationTriggerBinding& binding:bindings){
        if(binding.animatorId!=animatorId||binding.trigger!=trigger)continue;
        for(auto& live:g.bpScripts){
            if(live.inst.dead||live.entityId!=binding.listenerId)continue;
            if(!live.inst.graph||!live.inst.graph->findEvent(binding.customEvent.c_str()))continue;
            Entity* listener=g.scene.byId(live.entityId);if(!listener)break;
            live.inst.entity=listener;BPContext context=makeBPCtx(listener,0,-1,0);
            live.inst.fireCustom(binding.customEvent.c_str(),context);break;
        }
    }
    bpProcessSpawnedScripts();
}

// interface-style message: safe no-op when target has no matching custom event
static void bpSendMsgCb(int targetEntityId, const char* eventName) {
    for (auto& ls : g.bpScripts) {
        if (ls.inst.dead || ls.entityId != targetEntityId) continue;
        Entity* e = g.scene.byId(targetEntityId);
        if (!e) { ls.inst.dead = true; continue; }
        ls.inst.entity = e;
        BPContext ctx = makeBPCtx(e, 0, -1, 0);
        ls.inst.fireCustom(eventName, ctx);
    }
}

static void bpProcessDestroys() {
    for (int id : g.bpDestroyQueue) {
        for (auto& ls : g.bpScripts) if (ls.entityId == id) ls.inst.dead = true;
        if (g.selectedId == id) g.selectedId = 0;
        if (g.scene.byId(id)) {
            addLog(0, "Blueprint: destroyed '%s'.", g.scene.byId(id)->name);
            std::vector<int> audioIds; g.scene.collectSubtree(id, audioIds);
            for (int audioId : audioIds) g.audio.stop(audioId);
            g.scene.removeEntity(id);
        }
    }
    g.bpDestroyQueue.clear();
}

static void bpSetupScripts() {
    g.graphCache.clear();
    g.bpScripts.clear();
    g.bpKeyEvents.clear();
    g.bpKeyReleases.clear();
    g.bpDestroyQueue.clear();
    g.bpSpawnScriptQueue.clear();
    g.bpSpawnOverrides.clear();
    g.animationTriggerBindings.clear();
    g.bpProcessingSpawns = false;
    g.bpWorldBegun = false;
    g.bpMouseDX = g.bpMouseDY = g.bpWheelAccum = 0;
    for (float& v : g.bpAxisValues) v = 0;
    for (bool& b : g.bpBindAxisActive) b = false;
    // Spawning appends entities while Blueprint code is executing. Reserve room
    // before Play so the executing Entity pointer remains stable in callbacks.
    if (g.scene.entities.capacity() < g.scene.entities.size() + 4096)
        g.scene.entities.reserve(g.scene.entities.size() + 4096);
    std::vector<int> initialIds;
    for (const Entity& e : g.scene.entities) if (entityBlueprintCount(e) > 0) initialIds.push_back(e.id);
    for (int id : initialIds) bpAddLiveScripts(id);
}

// ═══ parent attachment (Play): children that the physics does not simulate
// follow their parent's rigid body, keeping the offset they had at Play start ═══
static bool followsParent(const Entity& e) {
    return !(e.hasPhysics && e.body->enabled && e.body->type == BodyType::Dynamic);
}

static Vec3 mulComponents(const Vec3& a, const Vec3& b) {
    return { a.x*b.x, a.y*b.y, a.z*b.z };
}

static Vec3 divComponents(const Vec3& a, const Vec3& b) {
    return { fabsf(b.x)>.000001f?a.x/b.x:a.x,
             fabsf(b.y)>.000001f?a.y/b.y:a.y,
             fabsf(b.z)>.000001f?a.z/b.z:a.z };
}

static void captureAttachments() {
    for (auto& e : g.scene.entities) {
        Entity* p = e.parentId ? g.scene.byId(e.parentId) : nullptr;
        if (!p) continue;
        e.attachPos = divComponents(p->body->quat.conjugate().rotate(e.body->position - p->body->position), p->scale);
        e.attachRot = p->body->quat.conjugate() * e.body->quat;
        e.attachScale = divComponents(e.scale, p->scale);
    }
}

static void propagateAttachments() {
    // walk each hierarchy with parents before children
    for (const auto& root : g.scene.entities) {
        if (root.parentId != 0) continue;
        std::vector<int> ids;
        g.scene.collectSubtree(root.id, ids);
        for (int id : ids) {
            Entity* e = g.scene.byId(id);
            if (!e || !e->parentId) continue;
            Entity* p = g.scene.byId(e->parentId);
            if (!p || !followsParent(*e)) continue;
            e->body->position = p->body->position + p->body->quat.rotate(mulComponents(e->attachPos, p->scale));
            e->body->quat = p->body->quat * e->attachRot;
            e->scale = mulComponents(e->attachScale, p->scale);
            g.scene.syncBodyShape(*e);
        }
    }
}

// ═══ play / stop ═══
static AnimatorControllerAsset* runtimeAnimatorController(const char* relativePath) {
    if (!relativePath || !relativePath[0]) return nullptr;
    auto found=g.runtimeAnimatorControllers.find(relativePath);
    if(found!=g.runtimeAnimatorControllers.end())return &found->second;
    std::string data; AnimatorControllerAsset controller;
    if(!readFile(g.projectDir+"\\"+relativePath,data)||!controller.deserialize(data))return nullptr;
    return &g.runtimeAnimatorControllers.emplace(relativePath,std::move(controller)).first->second;
}

static AnimationClipAsset* runtimeAnimationClip(const std::string& relativePath) {
    if(relativePath.empty())return nullptr;
    auto found=g.runtimeAnimationClips.find(relativePath);
    if(found!=g.runtimeAnimationClips.end())return &found->second;
    std::string data; AnimationClipAsset clip;
    if(!readFile(g.projectDir+"\\"+relativePath,data)||!clip.deserialize(data))return nullptr;
    return &g.runtimeAnimationClips.emplace(relativePath,std::move(clip)).first->second;
}

static unsigned long long animatorBaseKey(int ownerId,int targetId) {
    return ((unsigned long long)(unsigned int)ownerId<<32)|(unsigned int)targetId;
}

static const App::AnimatorBaseTransform& ensureRuntimeAnimatorBase(Entity& owner,Entity& target) {
    unsigned long long key=animatorBaseKey(owner.id,target.id);
    auto found=g.runtimeAnimatorBases.find(key);if(found!=g.runtimeAnimatorBases.end())return found->second;
    App::AnimatorBaseTransform base;Entity* parent=target.parentId?g.scene.byId(target.parentId):nullptr;
    if(parent&&parent->body){
        base.parentId=parent->id;
        base.position=divComponents(parent->body->quat.conjugate().rotate(target.body->position-parent->body->position),parent->scale);
        base.rotation=(parent->body->quat.conjugate()*target.body->quat).normalized();
        base.scale=divComponents(target.scale,parent->scale);
        base.referenceParentRotation=parent->body->quat;
        base.referenceParentScale=parent->scale;
    }else{
        base.position=target.body->position;base.rotation=target.body->quat;base.scale=target.scale;
    }
    return g.runtimeAnimatorBases.emplace(key,base).first->second;
}

static bool isFirstAnimatorTargetNamed(Entity& owner,const Entity& target,const std::string& name) {
    if(name.empty())return false;
    std::vector<int> subtree;g.scene.collectSubtree(owner.id,subtree);
    for(int id:subtree){
        const Entity* candidate=g.scene.byId(id);
        if(candidate&&name==candidate->name)return candidate->id==target.id;
    }
    return false;
}

static bool sampleRuntimeAnimation(Entity& owner,Entity& target,const AnimationClipAsset& clip,float time,bool mirror,
                                   Vec3& desiredPos,Quat& desiredRot,Vec3& desiredScale) {
    if(clip.keys.empty())return false;
    std::vector<std::pair<int,std::string>> tracks;
    for(const AnimationKey& key:clip.keys){
        std::pair<int,std::string> track{key.entityId,key.objectName};
        if(std::find(tracks.begin(),tracks.end(),track)==tracks.end())tracks.push_back(track);
    }
    const std::pair<int,std::string>* chosen=nullptr;
    // Runtime binding is name-based and scoped to the object that owns the
    // Animator plus its descendants. Scene IDs are editor-instance details and
    // cannot identify the same object reliably in another prefab instance.
    // With duplicate names only the first object in hierarchy order is bound,
    // so one animation track can never drive multiple objects accidentally.
    for(const auto& track:tracks)
        if(!track.second.empty()&&track.second==target.name&&isFirstAnimatorTargetNamed(owner,target,track.second)){
            chosen=&track;break;
        }
    // Compatibility for version-1 clips, which did not store an object name.
    if(!chosen)for(const auto& track:tracks)
        if(track.second.empty()&&track.first!=0&&track.first==target.id){chosen=&track;break;}
    if(!chosen&&target.id==owner.id&&tracks.size()==1&&tracks.front().second.empty())chosen=&tracks.front();
    if(!chosen)return false;
        const App::AnimatorBaseTransform& base=ensureRuntimeAnimatorBase(owner,target);
        AnimationKey first=clip.evaluateTrack(0,chosen->first,chosen->second);
        AnimationKey current=clip.evaluateTrack(time,chosen->first,chosen->second);
        Vec3 delta=current.position-first.position;
        Quat deltaRotation=(current.rotation*first.rotation.conjugate()).normalized();
        // Version-2 clips store world-space keys. Project their recorded delta
        // into the parent's reference space so they also keep following a
        // moving/rotating/scaling parent. Version-3 keys are already local.
        if(base.parentId&&!(first.localSpace&&current.localSpace)){
            delta=divComponents(base.referenceParentRotation.conjugate().rotate(delta),base.referenceParentScale);
            deltaRotation=(base.referenceParentRotation.conjugate()*deltaRotation*base.referenceParentRotation).normalized();
        }
        if(mirror){delta.x=-delta.x;deltaRotation.y=-deltaRotation.y;deltaRotation.z=-deltaRotation.z;}
        Vec3 posePosition=base.position+delta;
        Quat poseRotation=(deltaRotation*base.rotation).normalized();
        Vec3 poseScale=mulComponents(base.scale,divComponents(current.scale,first.scale));
        Entity* parent=base.parentId?g.scene.byId(base.parentId):nullptr;
        if(parent&&parent->body){
            desiredPos=parent->body->position+parent->body->quat.rotate(mulComponents(posePosition,parent->scale));
            desiredRot=(parent->body->quat*poseRotation).normalized();
            desiredScale=mulComponents(poseScale,parent->scale);
        }else{
            desiredPos=posePosition;desiredRot=poseRotation;desiredScale=poseScale;
        }
        return true;
}

static Quat animatorNlerp(Quat a,Quat b,float t){float dot=a.x*b.x+a.y*b.y+a.z*b.z+a.w*b.w;if(dot<0){b.x=-b.x;b.y=-b.y;b.z=-b.z;b.w=-b.w;}return Quat{a.x+(b.x-a.x)*t,a.y+(b.y-a.y)*t,a.z+(b.z-a.z)*t,a.w+(b.w-a.w)*t}.normalized();}

static void applyRuntimeAnimationBlend(Entity& owner,const AnimationClipAsset* from,float fromTime,bool fromMirror,
                                       const AnimationClipAsset& to,float toTime,bool toMirror,float alpha) {
    std::vector<int> subtree;g.scene.collectSubtree(owner.id,subtree);
    // Capture every bind pose before moving the root. Otherwise a child's base
    // could accidentally be sampled after its animated parent has moved.
    for(int targetId:subtree){Entity* target=g.scene.byId(targetId);if(target&&target->body)ensureRuntimeAnimatorBase(owner,*target);}
    for(int targetId:subtree){
        Entity* target=g.scene.byId(targetId);if(!target||!target->body)continue;
        Vec3 bp,bs,ap,as;Quat br,ar;
        bool haveTo=sampleRuntimeAnimation(owner,*target,to,toTime,toMirror,bp,br,bs);if(!haveTo)continue;
        bool haveFrom=from&&sampleRuntimeAnimation(owner,*target,*from,fromTime,fromMirror,ap,ar,as);
        Vec3 desiredPos=haveFrom?ap+(bp-ap)*alpha:bp;
        Quat desiredRot=haveFrom?animatorNlerp(ar,br,alpha):br;
        Vec3 desiredScale=haveFrom?as+(bs-as)*alpha:bs;
        Vec3 moveDelta=desiredPos-target->body->position;Quat oldRotation=target->body->quat;Vec3 oldScale=target->scale;
        target->body->position=desiredPos;target->body->quat=desiredRot;target->scale=desiredScale;
        target->body->velocity={};target->body->angularVelocity={};
        g.scene.moveDescendants(targetId,moveDelta);g.scene.rotateDescendants(targetId,desiredPos,oldRotation,desiredRot);g.scene.scaleDescendants(targetId,desiredPos,desiredRot,oldScale,desiredScale);
        Entity* parent=target->parentId?g.scene.byId(target->parentId):nullptr;
        if(parent&&parent->body){
            target->attachPos=divComponents(parent->body->quat.conjugate().rotate(desiredPos-parent->body->position),parent->scale);
            target->attachRot=(parent->body->quat.conjugate()*desiredRot).normalized();
            target->attachScale=divComponents(desiredScale,parent->scale);
        }
        target->body->updateInertiaWorld();g.scene.syncBodyShape(*target);
    }
}

static void applyRuntimeAnimation(Entity& owner,const AnimationClipAsset& clip,float time) { applyRuntimeAnimationBlend(owner,nullptr,0,false,clip,time,false,1); }

static void startAnimators() {
    g.runtimeAnimatorControllers.clear();g.runtimeAnimationClips.clear();g.runtimeAnimatorBases.clear();
    for(Entity& e:g.scene.entities){
        e.animatorRuntimeState=0;e.animatorRuntimeTime=0;e.animatorRuntimePlaying=false;e.animatorRuntimePreviousState=0;e.animatorRuntimeEventState=0;
        e.animatorRuntimePreviousTime=0;e.animatorRuntimeTransitionTime=0;e.animatorRuntimeTransitionDuration=0;e.animatorRuntimeParameters.clear();
        if(!e.hasAnimator||!e.animatorController[0])continue;
        AnimatorControllerAsset* controller=runtimeAnimatorController(e.animatorController);
        if(!controller||!controller->byId(controller->defaultState)){
            addLog(2,"Invalid Animator on %s: %s",e.name,e.animatorController);continue;
        }
        e.animatorRuntimeState=controller->defaultState;
        e.animatorRuntimePlaying=e.animatorPlayOnAwake;
        for(const AnimatorParameter& parameter:controller->parameters)e.animatorRuntimeParameters[parameter.name]=parameter.defaultValue;
    }
}

static bool animatorTransitionCondition(Entity& e,const AnimatorTransition& transition) {
    if(transition.condition==0)return true;
    auto found=e.animatorRuntimeParameters.find(transition.parameter);float value=found==e.animatorRuntimeParameters.end()?0.0f:found->second;
    if(transition.condition==1)return value!=0;
    if(transition.condition==2)return value==0;
    if(transition.condition==3)return value>transition.threshold;
    if(transition.condition==4)return value<transition.threshold;
    if(transition.condition==5)return value!=0;
    return false;
}

static float animatorBlendAlpha(float t,int curve) {
    t=clampf(t,0,1);
    if(curve==1)return t*t*(3-2*t);
    if(curve==2)return t*t;
    if(curve==3){float u=1-t;return 1-u*u;}
    return t;
}

static void emitAnimationTriggers(Entity& owner,const AnimationClipAsset* clip,float from,float to,bool includeStart=false) {
    if(!clip||clip->events.empty())return;
    const float epsilon=.00001f;
    if(includeStart)for(const AnimationEventKey& event:clip->events)if(event.time<=epsilon)g.pendingAnimationTriggers.push_back({owner.id,event.name});
    if(to<=from+epsilon)return;
    if(!clip->loop){
        for(const AnimationEventKey& event:clip->events)
            if(event.time>from+epsilon&&event.time<=to+epsilon)g.pendingAnimationTriggers.push_back({owner.id,event.name});
        return;
    }
    float duration=(std::max)(.01f,clip->length);
    int firstCycle=(int)floorf(from/duration),lastCycle=(int)floorf(to/duration);
    if(lastCycle-firstCycle>64)firstCycle=lastCycle-64;
    for(int cycle=firstCycle;cycle<=lastCycle;cycle++)for(const AnimationEventKey& event:clip->events){
        float absolute=cycle*duration+event.time;
        if(absolute>from+epsilon&&absolute<=to+epsilon)g.pendingAnimationTriggers.push_back({owner.id,event.name});
    }
}

static void updateAnimators(float dt) {
    g.pendingAnimationTriggers.clear();
    for(Entity& e:g.scene.entities){
        if(!e.hasAnimator||!e.animatorRuntimePlaying||!e.animatorController[0])continue;
        AnimatorControllerAsset* controller=runtimeAnimatorController(e.animatorController);if(!controller)continue;
        const AnimatorState* state=controller->byId(e.animatorRuntimeState);if(!state)continue;
        // A state without a clip is valid: it can be used as a routing/pass-through
        // state. Transitions must still be evaluated (previously we returned here,
        // so Enter could remain stuck forever on "No clip").
        AnimationClipAsset* clip=runtimeAnimationClip(state->clip);
        float ownerSpeed=(std::max)(0.0f,e.animatorSpeed);
        if(e.animatorRuntimePreviousState){
            const AnimatorState* previous=controller->byId(e.animatorRuntimePreviousState);
            AnimationClipAsset* previousClip=previous?runtimeAnimationClip(previous->clip):nullptr;
            float oldPreviousTime=e.animatorRuntimePreviousTime,oldCurrentTime=e.animatorRuntimeTime;
            e.animatorRuntimePreviousTime+=ownerSpeed*(previous?(std::max)(0.0f,previous->speed):1.0f)*dt;
            e.animatorRuntimeTime+=ownerSpeed*(std::max)(0.0f,state->speed)*dt;
            emitAnimationTriggers(e,previousClip,oldPreviousTime,e.animatorRuntimePreviousTime,false);
            bool currentStarted=e.animatorRuntimeEventState!=state->id;
            emitAnimationTriggers(e,clip,oldCurrentTime,e.animatorRuntimeTime,currentStarted);
            e.animatorRuntimeEventState=state->id;
            e.animatorRuntimeTransitionTime+=dt;
            float raw=e.animatorRuntimeTransitionDuration<=.0001f?1.0f:e.animatorRuntimeTransitionTime/e.animatorRuntimeTransitionDuration;
            const AnimatorTransition* transition=controller->transition(e.animatorRuntimePreviousState,e.animatorRuntimeState);
            float alpha=animatorBlendAlpha(raw,transition?transition->blendCurve:0);
            if(clip)applyRuntimeAnimationBlend(e,previousClip,e.animatorRuntimePreviousTime,previous?previous->mirror:false,*clip,e.animatorRuntimeTime,state->mirror,alpha);
            if(raw>=1)e.animatorRuntimePreviousState=0;
            continue;
        }
        float oldStateTime=e.animatorRuntimeTime;
        e.animatorRuntimeTime+=ownerSpeed*(std::max)(0.0f,state->speed)*dt;
        bool stateStarted=e.animatorRuntimeEventState!=state->id;
        emitAnimationTriggers(e,clip,oldStateTime,e.animatorRuntimeTime,stateStarted);
        e.animatorRuntimeEventState=state->id;
        // Empty states have no duration; consider Exit Time immediately reached.
        float normalized=clip?e.animatorRuntimeTime/(std::max)(.01f,clip->length):1.0f;
        const AnimatorTransition* chosen=nullptr;
        for(const AnimatorTransition& transition:controller->transitions){
            if(transition.from!=state->id||!controller->byId(transition.to))continue;
            if(transition.hasExitTime&&normalized+1e-5f<transition.exitTime)continue;
            if(animatorTransitionCondition(e,transition)){chosen=&transition;break;}
        }
        if(chosen){
            const AnimatorState* target=controller->byId(chosen->to);
            AnimationClipAsset* targetClip=target?runtimeAnimationClip(target->clip):nullptr;
            bool canBlend=clip&&targetClip&&chosen->duration>.0001f;
            e.animatorRuntimePreviousState=canBlend?state->id:0;e.animatorRuntimePreviousTime=e.animatorRuntimeTime;
            e.animatorRuntimeState=chosen->to;e.animatorRuntimeTime=0;e.animatorRuntimeTransitionTime=0;e.animatorRuntimeTransitionDuration=(std::max)(0.0f,chosen->duration);
            e.animatorRuntimeEventState=target?target->id:0;
            emitAnimationTriggers(e,targetClip,0,0,true);
            if(chosen->condition==5)e.animatorRuntimeParameters[chosen->parameter]=0;
            if(targetClip)applyRuntimeAnimationBlend(e,canBlend?clip:nullptr,e.animatorRuntimePreviousTime,state->mirror,*targetClip,0,target->mirror,canBlend?0.0f:1.0f);
            continue;
        }
        if(!clip)continue;
        if(e.animatorRuntimeTime>clip->length){if(clip->loop)e.animatorRuntimeTime=fmodf(e.animatorRuntimeTime,(std::max)(.01f,clip->length));else e.animatorRuntimeTime=clip->length;}
        applyRuntimeAnimationBlend(e,nullptr,0,false,*clip,e.animatorRuntimeTime,state->mirror,1);
    }
    std::vector<std::pair<int,std::string>> pending=std::move(g.pendingAnimationTriggers);
    g.pendingAnimationTriggers.clear();
    for(const auto& event:pending)bpFireAnimationTrigger(event.first,event.second);
}

static void startBehaviors() {
    for (auto& e : g.scene.entities) {
        if (e.behavior == BH_PUSH_START && e.body->type == BodyType::Dynamic) {
            e.body->wake();
            e.body->velocity = { e.bp[0], e.bp[1], e.bp[2] };
        }
    }
}

// ═══ Play mouse capture (FPS-style: cursor locked/hidden in the viewport) ═══
static bool s_playMouseFresh = false;
static void capturePlayMouse() {
    g.playMouseCaptured = true;
    s_playMouseFresh = true;
}
static void releasePlayMouse() {
    if (!g.playMouseCaptured) return;
    g.playMouseCaptured = false;
    ClipCursor(nullptr);
    while (ShowCursor(TRUE) < 0) {}
    addLog(0, "Mouse released (Shift+F1). Click in the viewport to take control again.");
}
static void updatePlayMouse() {
    if (g.mode != Mode::Play || !g.playMouseCaptured) return;
    UIRect v = viewportRect();
    if (v.w < 8 || v.h < 8) return;
    POINT tl = { (LONG)v.x, (LONG)v.y }, br = { (LONG)(v.x + v.w), (LONG)(v.y + v.h) };
    ClientToScreen(g.hwnd, &tl);
    ClientToScreen(g.hwnd, &br);
    RECT clip = { tl.x, tl.y, br.x, br.y };
    ClipCursor(&clip);
    while (ShowCursor(FALSE) >= 0) {}
    POINT center = { (LONG)(v.x + v.w / 2), (LONG)(v.y + v.h / 2) };
    ClientToScreen(g.hwnd, &center);
    if (s_playMouseFresh) {
        s_playMouseFresh = false;   // no jump on the first captured frame
    } else {
        POINT p;
        GetCursorPos(&p);
        g.bpMouseDX += (float)(p.x - center.x);
        g.bpMouseDY += (float)(p.y - center.y);
    }
    SetCursorPos(center.x, center.y);
}

// ═══ free-fly camera (Play with no game camera, or paused RMB "eject") ═══
static Quat flyRotQ(float yaw, float pitch) {
    return Quat::axisAngle({ 0, 1, 0 }, yaw) * Quat::axisAngle({ 1, 0, 0 }, pitch);
}
static void dirToYawPitch(Vec3 dir, float& yaw, float& pitch) {
    float len = dir.length();
    if (len > 1e-5f) dir = dir * (1.0f / len);
    pitch = asinf(clampf(dir.y, -1.0f, 1.0f));
    yaw = atan2f(-dir.x, -dir.z);
}

static bool sceneHasCamera() {
    for (auto& e : g.scene.entities) if (e.isCamera) return true;
    return false;
}

// Seed the paused/ejected camera immediately on RMB down. Waiting for the next
// frame used to let the first WM_MOUSEMOVE operate on stale yaw/pitch values.
static void beginFreeLookFromPlayView() {
    if (g.flyActive) return;
    Entity* gameCam = nullptr;
    for (auto& e : g.scene.entities) if (e.isCamera) { gameCam = &e; break; }
    if (gameCam && gameCam->body) {
        g.flyPos = gameCam->body->position + Vec3{ 0, gameCam->camOffsetY, 0 };
        dirToYawPitch(gameCam->body->quat.rotate({ 0, 0, -1 }), g.flyYaw, g.flyPitch);
    } else {
        g.flyPos = g.camera.eye;
        dirToYawPitch(g.camera.target - g.camera.eye, g.flyYaw, g.flyPitch);
    }
    g.flyActive = true;
}

// sets g.camera.fp* for the Play view: game camera, or a free-fly camera when there
// is none (or while paused and holding RMB to look around)
static void updatePlayCamera(float dt) {
    g.camera.fpActive = false;
    if (g.mode != Mode::Play) {
        g.flyActive = false; g.flyLook = false;
        g.camera.nearZ = 0.1f; g.camera.farZ = 500.0f;
        return;
    }
    Entity* gameCam = nullptr;
    for (auto& e : g.scene.entities) if (e.isCamera) { gameCam = &e; break; }

    bool wantFree = !gameCam || (g.paused && g.flyLook);
    if (wantFree) {
        if (!g.flyActive) beginFreeLookFromPlayView();
        if (g.flyLook) {   // fly with WASD (E/Q up/down, Shift = faster) while looking
            Quat rot = flyRotQ(g.flyYaw, g.flyPitch);
            Vec3 fwd = rot.rotate({ 0, 0, -1 }), right = rot.rotate({ 1, 0, 0 }), up = { 0, 1, 0 };
            float speed = ((GetAsyncKeyState(VK_SHIFT) & 0x8000) ? 26.0f : 9.0f) * dt;
            if (GetAsyncKeyState('W') & 0x8000) g.flyPos = g.flyPos + fwd * speed;
            if (GetAsyncKeyState('S') & 0x8000) g.flyPos = g.flyPos - fwd * speed;
            if (GetAsyncKeyState('D') & 0x8000) g.flyPos = g.flyPos + right * speed;
            if (GetAsyncKeyState('A') & 0x8000) g.flyPos = g.flyPos - right * speed;
            if (GetAsyncKeyState('E') & 0x8000) g.flyPos = g.flyPos + up * speed;
            if (GetAsyncKeyState('Q') & 0x8000) g.flyPos = g.flyPos - up * speed;
        }
        g.camera.fpActive = true;
        g.camera.fpEye = g.flyPos;
        g.camera.fpRot = flyRotQ(g.flyYaw, g.flyPitch);
        g.camera.fpFov = 70;
    } else {
        g.flyActive = false;
        if (gameCam) {
            g.camera.fpActive = true;
            g.camera.fpEye = gameCam->body->position + Vec3{ 0, gameCam->camOffsetY, 0 };
            g.camera.fpRot = gameCam->body->quat;
            g.camera.fpFov = gameCam->camFov;
            g.camera.nearZ = gameCam->camLinearClipping ? clampf(gameCam->camNearClip, .001f, 1000.0f) : .1f;
            g.camera.farZ = gameCam->camLinearClipping
                          ? (std::max)(g.camera.nearZ + .01f, gameCam->camClipDistance) : 500.0f;
        }
    }
}

// Edit-mode fly navigation: hold RMB in a viewport (main or popped-out), then
// WASD moves, Q/E drop/rise, Shift = faster. Speed scales with zoom so it stays
// usable whether you're close in or way out. Runs before camera.update().
static void updateEditFly(float dt) {
    if (g.inHub || g.mode != Mode::Edit || g.activeDoc != 0) return;
    if (!g.orbiting) return;   // only while right-dragging the viewport
    float base = (GetAsyncKeyState(VK_SHIFT) & 0x8000) ? 26.0f : 9.0f;
    float s = base * clampf(g.camera.distance / 15.0f, 0.5f, 8.0f) * dt;
    float f = 0, r = 0, u = 0;
    if (GetAsyncKeyState('W') & 0x8000) f += s;
    if (GetAsyncKeyState('S') & 0x8000) f -= s;
    if (GetAsyncKeyState('D') & 0x8000) r += s;
    if (GetAsyncKeyState('A') & 0x8000) r -= s;
    if (GetAsyncKeyState('E') & 0x8000) u += s;
    if (GetAsyncKeyState('Q') & 0x8000) u -= s;
    if (f != 0 || r != 0 || u != 0) g.camera.flyMove(f, r, u);
}

static Vec3 audioListenerPosition() {
    for (const Entity& e : g.scene.entities)
        if (e.isCamera && e.body) return e.body->position + Vec3{ 0, e.camOffsetY, 0 };
    return g.camera.fpActive ? g.camera.fpEye : g.camera.eye;
}

static const AudioClassAsset* audioClassAsset(const char* relativePath) {
    if (!relativePath || !relativePath[0]) return nullptr;
    auto found = g.audioClassCache.find(relativePath);
    if (found != g.audioClassCache.end()) return &found->second;
    std::string data;
    AudioClassAsset asset;
    if (!readFile(g.projectDir + "\\" + relativePath, data) || !asset.deserialize(data)) return nullptr;
    return &g.audioClassCache.emplace(relativePath, asset).first->second;
}

static const AudioAttenuationAsset* audioAttenuationAsset(const char* relativePath) {
    if (!relativePath || !relativePath[0]) return nullptr;
    auto found = g.audioAttenuationCache.find(relativePath);
    if (found != g.audioAttenuationCache.end()) return &found->second;
    std::string data;
    AudioAttenuationAsset asset;
    if (!readFile(g.projectDir + "\\" + relativePath, data) || !asset.deserialize(data)) return nullptr;
    return &g.audioAttenuationCache.emplace(relativePath, asset).first->second;
}

static const AudioConcurrencyAsset* audioConcurrencyAsset(const char* relativePath) {
    if (!relativePath || !relativePath[0]) return nullptr;
    auto found = g.audioConcurrencyCache.find(relativePath);
    if (found != g.audioConcurrencyCache.end()) return &found->second;
    std::string data;
    AudioConcurrencyAsset asset;
    if (!readFile(g.projectDir + "\\" + relativePath, data) || !asset.deserialize(data)) return nullptr;
    return &g.audioConcurrencyCache.emplace(relativePath, asset).first->second;
}

static float audioReverbAmount(const Vec3& listener) {
    float amount = 0.0f;
    for (const Entity& zone : g.scene.entities) {
        if (!zone.hasReverb || !zone.body || zone.reverbRadius <= 0.01f) continue;
        float distance = (listener - zone.body->position).length();
        if (distance >= zone.reverbRadius) continue;
        float blend = 1.0f - distance / zone.reverbRadius;
        float decayEnergy = clampf(zone.reverbDecay / 4.0f, 0.1f, 1.0f);
        amount = (std::max)(amount, clampf(zone.reverbWet, 0.0f, 1.0f) * blend * (0.65f + 0.35f * decayEnergy));
    }
    return amount;
}

static float audioSourceGain(const Entity& e, const Vec3& listener) {
    const AudioClassAsset* cls = audioClassAsset(e.audioClass);
    float gain = clampf(e.audioVolume, 0.0f, 2.0f) * (cls ? cls->volume : 1.0f);
    const AudioAttenuationAsset* settings = audioAttenuationAsset(e.audioAttenuation);
    bool spatial = settings ? settings->spatial : e.audioSpatial;
    float reverb = audioReverbAmount(listener);
    if (!spatial || !e.body) return clampf(gain * (1.0f + reverb * 0.18f), 0.0f, 2.0f);
    float minD = settings ? settings->minDistance : e.audioMinDistance;
    float maxD = settings ? settings->maxDistance : e.audioMaxDistance;
    minD = minD < 0.01f ? 0.01f : minD;
    maxD = maxD < minD + 0.01f ? minD + 0.01f : maxD;
    float d = (e.body->position - listener).length();
    float attenuation = 1.0f;
    if (d >= maxD) attenuation = 0.0f;
    else if (d > minD) {
        float t = (d - minD) / (maxD - minD);
        int falloff = settings ? settings->falloff : 0;
        if (falloff == 1) {
            float edge = minD / maxD;
            attenuation = (minD / d - edge) / (1.0f - edge);
        } else if (falloff == 2) {
            attenuation = (1.0f - t) * (1.0f - t);
        } else attenuation = 1.0f - t;
    }
    // MCI has no portable DSP bus. Preserve the dry signal and model the wet
    // tail as extra distance energy while the listener is inside a Reverb Zone.
    float wetTail = reverb > 0.0f ? sqrtf((std::max)(0.0f, attenuation)) * reverb * 0.30f : 0.0f;
    return clampf(gain * (attenuation + wetTail), 0.0f, 2.0f);
}

static float audioFadeMultiplier(int entityId) {
    auto it = g.audioFades.find(entityId);
    return it == g.audioFades.end() ? 1.0f : clampf(it->second.multiplier, 0.0f, 1.0f);
}

static bool playAudioSource(Entity& e, bool resetFade) {
    if (!e.hasAudio || !e.audioClip[0]) return false;
    if (resetFade) g.audioFades.erase(e.id);
    if (const AudioConcurrencyAsset* concurrency = audioConcurrencyAsset(e.audioConcurrency)) {
        std::vector<int>& owners = g.audioConcurrencyOwners[e.audioConcurrency];
        owners.erase(std::remove_if(owners.begin(), owners.end(), [&](int id) {
            return id != e.id && !g.audio.hasVoice(id);
        }), owners.end());
        bool alreadyCounted = std::find(owners.begin(), owners.end(), e.id) != owners.end();
        if (!alreadyCounted && (int)owners.size() >= concurrency->maxVoices) {
            if (concurrency->resolution == 0) {
                addLog(0, "Audio Concurrency '%s': new voice rejected.", e.audioConcurrency);
                return false;
            }
            int oldest = owners.front();
            owners.erase(owners.begin());
            g.audio.stop(oldest);
            g.audioFades.erase(oldest);
        }
        if (!alreadyCounted) owners.push_back(e.id);
    }
    std::string abs = g.projectDir + "\\" + e.audioClip;
    float gain = audioSourceGain(e, audioListenerPosition()) * audioFadeMultiplier(e.id);
    if (g.audio.play(e.id, abs, e.audioLoop, gain)) return true;
    if (e.audioConcurrency[0]) {
        std::vector<int>& owners = g.audioConcurrencyOwners[e.audioConcurrency];
        owners.erase(std::remove(owners.begin(), owners.end(), e.id), owners.end());
    }
    addLog(2, "Audio Source '%s': %s", e.name, g.audio.lastError().c_str());
    return false;
}

static void setAudioSourceVolume(Entity& e, float volume) {
    e.audioVolume = clampf(volume, 0.0f, 2.0f);
    if (g.audio.hasVoice(e.id))
        g.audio.setGain(e.id, audioSourceGain(e, audioListenerPosition()) * audioFadeMultiplier(e.id));
}

static void setAudioSourceClip(Entity& e, const char* clip) {
    g.audioFades.erase(e.id);
    e.hasAudio = true;
    std::string rel = clip ? clip : "";
    std::replace(rel.begin(), rel.end(), '/', '\\');
    while (rel.rfind(".\\", 0) == 0) rel.erase(0, 2);
    snprintf(e.audioClip, sizeof(e.audioClip), "%s", rel.c_str());
    // Stop without closing the decoder. A following Play reuses it through the
    // per-source cache, which avoids codec/file-open stalls for alternating
    // footsteps. Clearing the clip still performs a complete stop.
    if (rel.empty()) g.audio.stop(e.id);
    else g.audio.prepareClipChange(e.id);
}

static void fadeInAudioSource(Entity& e, float duration) {
    if (!e.hasAudio || !e.audioClip[0]) {
        addLog(2, "Fade In: Audio Source '%s' has no clip.", e.name);
        return;
    }
    duration = duration < 0.0f ? 0.0f : duration;
    App::AudioFade fade;
    fade.multiplier = fade.start = duration <= 0.0f ? 1.0f : 0.0f;
    fade.target = 1.0f;
    fade.duration = duration;
    fade.stopWhenDone = false;
    g.audioFades[e.id] = fade;
    if (!playAudioSource(e, false)) g.audioFades.erase(e.id);
}

static void fadeOutAudioSource(Entity& e, float duration) {
    if (!g.audio.hasVoice(e.id)) return;
    if (duration <= 0.0f) {
        g.audio.stop(e.id);
        g.audioFades.erase(e.id);
        return;
    }
    App::AudioFade fade;
    fade.multiplier = fade.start = audioFadeMultiplier(e.id);
    fade.target = 0.0f;
    fade.duration = duration;
    fade.stopWhenDone = true;
    g.audioFades[e.id] = fade;
}

static void startAudioSources() {
    g.audio.stopAll();
    g.audioFades.clear();
    g.audioConcurrencyOwners.clear();
    for (Entity& e : g.scene.entities)
        if (e.hasAudio && e.audioPlayOnAwake && e.audioClip[0]) playAudioSource(e);
}

static void updateAudioSources(float dt) {
    g.audio.setPaused(g.mode == Mode::Play && g.paused);
    if (g.mode == Mode::Play && !g.paused) {
        for (auto it = g.audioFades.begin(); it != g.audioFades.end();) {
            Entity* e = g.scene.byId(it->first);
            if (!e || !g.audio.hasVoice(it->first)) {
                it = g.audioFades.erase(it);
                continue;
            }
            App::AudioFade& fade = it->second;
            fade.elapsed += dt;
            float alpha = fade.duration <= 0.0f ? 1.0f : clampf(fade.elapsed / fade.duration, 0.0f, 1.0f);
            fade.multiplier = fade.start + (fade.target - fade.start) * alpha;
            if (alpha >= 1.0f && fade.stopWhenDone) {
                g.audio.stop(it->first);
                it = g.audioFades.erase(it);
                continue;
            }
            if (alpha >= 1.0f && !fade.stopWhenDone) {
                it = g.audioFades.erase(it);
                continue;
            }
            ++it;
        }
    }
    // MCI status/setaudio are synchronous Windows calls. Querying every source
    // every rendered frame caused visible stalls exactly when footsteps played.
    // The voice map is authoritative and spatial/fade gain is updated at 20 Hz,
    // which is perceptually smooth without blocking the simulation loop.
    g.audioGainUpdateTimer -= dt;
    if (g.audioGainUpdateTimer <= 0.0f) {
        g.audioGainUpdateTimer = 0.05f;
        Vec3 listener = audioListenerPosition();
        for (const Entity& e : g.scene.entities)
            if (e.hasAudio && g.audio.hasVoice(e.id))
                g.audio.setGain(e.id, audioSourceGain(e, listener) * audioFadeMultiplier(e.id));
    }
}

static void saveDirtyBlueprintsForPlay() {
    for (auto& document : g.bpDocs) {
        if (!document || !document->dirty || document->curPath.empty()) continue;
        if (document->saveTo(g.projectDir + "\\" + document->curPath))
            addLog(0, "Blueprint updated for Play: %s", document->curPath.c_str());
        else addLog(2, "Could not save the Blueprint before Play: %s", document->curPath.c_str());
    }
    // widgets too: Play instantiates them from disk, designer tree and graph alike
    for (auto& document : g.widgetDocs) {
        if (!document || !document->isDirty() || document->curPath.empty()) continue;
        if (document->save()) addLog(0, "Widget updated for Play: %s", document->curPath.c_str());
        else addLog(2, "Could not save the Widget before Play: %s", document->curPath.c_str());
    }
    for (Entity& entity : g.scene.entities) ensureBlueprintRequirements(entity);
}

static bool appActorIsBlueprintClass(const Entity& actor, const std::string& requested) {
    std::string current = actor.graphPath;
    for (int depth = 0; depth < 16 && !current.empty(); depth++) {
        if (_stricmp(current.c_str(), requested.c_str()) == 0) return true;
        std::string data;
        BPGraph graph;
        if (!readFile(g.projectDir + "\\" + current, data) || !graph.deserialize(data)) break;
        current = graph.parentAsset;
    }
    return false;
}

static void setupGameplayFramework() {
    g.gameModeEntity = g.gameInstanceEntity = g.playerControllerEntity = g.playerPawnEntity = 0;
    // the level's own GameMode wins; otherwise fall back to the project default
    std::string gameModePath = !g.scene.gameModePath.empty() ? g.scene.gameModePath : g.defaultGameModeAsset;
    BPGraph gameMode;
    bool hasGameMode = !gameModePath.empty() && bpLoadResolvedGraph(g.projectDir, gameModePath, gameMode);
    if (hasGameMode) {
        Entity& object = g.scene.spawnEmpty("GameMode (Runtime)");
        g.gameModeEntity = object.id;
        object.tags.push_back("GameMode");
        snprintf(object.graphPath, sizeof(object.graphPath), "%s", gameModePath.c_str());
    } else if (!gameModePath.empty()) addLog(2, "Invalid GameMode: %s", gameModePath.c_str());

    if (!g.gameInstanceAsset.empty()) {
        BPGraph instanceClass;
        if (bpLoadResolvedGraph(g.projectDir, g.gameInstanceAsset, instanceClass)) {
            Entity& object = g.scene.spawnEmpty("GameInstance (Runtime)");
            g.gameInstanceEntity = object.id;
            object.tags.push_back("GameInstance");
            snprintf(object.graphPath, sizeof(object.graphPath), "%s", g.gameInstanceAsset.c_str());
        } else addLog(2, "Invalid GameInstance: %s", g.gameInstanceAsset.c_str());
    }

    for (const Entity& actor : g.scene.entities) {
        for (const std::string& tag : actor.tags) if (_stricmp(tag.c_str(), "Player") == 0) { g.playerPawnEntity = actor.id; break; }
        if (g.playerPawnEntity) break;
    }
    if (!g.playerPawnEntity && hasGameMode && !gameMode.defaultPawnClass.empty()) {
        for (const Entity& actor : g.scene.entities) if (appActorIsBlueprintClass(actor, gameMode.defaultPawnClass)) {
            g.playerPawnEntity = actor.id;
            break;
        }
    }
    if (!g.playerPawnEntity) for (const Entity& actor : g.scene.entities) if (_stricmp(actor.name, "Player") == 0) {
        g.playerPawnEntity = actor.id;
        break;
    }

    Entity& controller = g.scene.spawnEmpty("PlayerController (Runtime)");
    g.playerControllerEntity = controller.id;
    controller.tags.push_back("PlayerController");
    if (hasGameMode && !gameMode.playerControllerClass.empty())
        snprintf(controller.graphPath, sizeof(controller.graphPath), "%s", gameMode.playerControllerClass.c_str());
    addLog(0, "Gameplay Framework: GameMode=%d, GameInstance=%d, PlayerController=%d, PlayerPawn=%d",
           g.gameModeEntity, g.gameInstanceEntity, g.playerControllerEntity, g.playerPawnEntity);
}

static void play() {
    if (g.mode == Mode::Play) return;
    g.navigationShowBeforePlay=g.navigation.show;
    g.animationPlaying = false;
    g.animationRecording = false;
    restoreAnimationPreview();
    saveDirtyBlueprintsForPlay();
    bool needsNavigation = false;
    for (Entity& e : g.scene.entities) {
        if (e.staticFlags & STATIC_MOVEMENT) {
            e.body->type = BodyType::Static; e.body->velocity = {}; e.body->angularVelocity = {}; e.body->setMass(0);
        }
        if (e.hasAIAgent) needsNavigation = true;
    }
    if (needsNavigation && !g.navigation.baked) bakeNavigation();
    g.scene.applyLayersToWorld();   // collision layer matrix into the physics world
    g.debugSegs.clear();
    g.snapshot = g.scene.serialize();
    setupGameplayFramework();
    g.mode = Mode::Play;
    g.paused = false;
    g.shots = 0;
    g.accumulator = 0;
    g.playTime = 0;
    captureAttachments();
    startAnimators();
    startBehaviors();
    bpSetupScripts();
    // the level's HUD widget is a runtime widget like any other, so its own
    // graph (Construct / Tick / mouse events) runs too
    g.hudWidgetHandle = 0;
    if (!g.scene.hudWidget.empty()) {
        g.hudWidgetHandle = createRuntimeWidget(g.scene.hudWidget.c_str());
        if (g.hudWidgetHandle) bpAddWidgetViewportCb(g.hudWidgetHandle);
    }
    bpFireAll(BP_EV_CONSTRUCT);
    g.bpWorldBegun = true;
    bpFireAll(BP_EV_START);
    bpProcessDestroys();
    startAudioSources();
    // no scene camera → free-fly camera seeded from the current editor view
    g.flyLook = false;
    bool hasCam = sceneHasCamera();
    g.flyActive = !hasCam;
    if (!hasCam) {
        g.flyPos = g.camera.eye;
        dirToYawPitch(g.camera.target - g.camera.eye, g.flyYaw, g.flyPitch);
    } else {
        capturePlayMouse();   // FPS mouse-look only when a game camera drives the view
    }
    if (!g.standaloneMode && g.playFullscreenOption) {
        g.playFullscreenActive = true;
        setWindowFullscreen(true);
    }
    addLog(1, hasCam
           ? "Simulation started: %d bodies, %d constraints, %d blueprints. (Shift+F1 releases the mouse)"
           : "Simulation started: %d bodies, %d constraints, %d blueprints. (no camera: fly with RMB + WASD)",
           (int)g.scene.world.bodies.size(), (int)g.scene.world.constraints.size(), (int)g.bpScripts.size());
}

// Tear down the running Play session (scripts, audio, animators, framework),
// preserving the GameInstance's persistent variables. Does NOT restore the
// scene, so both Stop (restores the snapshot) and Open Level (loads a new
// scene) can reuse it.
static void teardownPlayRuntime() {
    releasePlayMouse();
    g.audio.stopAll();
    g.audioFades.clear();
    for (const auto& live : g.bpScripts) if (live.entityId == g.gameInstanceEntity) {
        g.persistentGameInstanceVars = live.inst.vars;
        break;
    }
    g.mode = Mode::Edit;
    g.paused = false;
    g.grabBody = nullptr;
    g.bpScripts.clear();
    g.graphCache.clear();
    g.bpSpawnScriptQueue.clear();
    g.bpSpawnOverrides.clear();
    g.bpWorldBegun = false;
    g.runtimeAnimatorControllers.clear();
    g.runtimeAnimationClips.clear();
    g.runtimeAnimatorBases.clear();
    g.animationTriggerBindings.clear();
    g.runtimeWidgets.clear();
    g.widgetGraphCache.clear();
    g.nextWidgetHandle = 1;
    g.hudWidgetHandle = 0;
    g.gameModeEntity = g.gameInstanceEntity = g.playerControllerEntity = g.playerPawnEntity = 0;
}

static void stopPlay() {
    if (g.mode != Mode::Play) return;
    teardownPlayRuntime();
    g.scene.deserialize(g.snapshot);
    g.navigation.show = g.navigationShowBeforePlay;
    if (g.playFullscreenActive) {
        g.playFullscreenActive = false;
        setWindowFullscreen(g.editorFullscreen);
    }
    addLog(0, "Simulation stopped: scene restored.");
}

// Open Level (Blueprint): swap to another scene at a safe point in the frame.
// The level name resolves to a .imp under the project (Levels\ first).
static void processPendingOpenLevel() {
    if (g.pendingOpenLevel.empty()) return;
    std::string req = g.pendingOpenLevel;
    g.pendingOpenLevel.clear();
    for (char& c : req) if (c == '/') c = '\\';
    bool hasImp = req.size() > 4 && _stricmp(req.c_str() + req.size() - 4, ".imp") == 0;
    std::vector<std::string> candidates;
    if (hasImp) { candidates.push_back(req); candidates.push_back("Levels\\" + req); }
    else { candidates.push_back("Levels\\" + req + ".imp"); candidates.push_back(req + ".imp"); }
    std::string abs, data;
    for (const std::string& rel : candidates) {
        std::error_code ec;
        std::string full = g.projectDir + "\\" + rel;
        if (fs::is_regular_file(full, ec)) { abs = full; break; }
    }
    if (abs.empty() || !readFile(abs, data)) { addLog(2, "Open Level: level not found: %s", req.c_str()); return; }
    bool wasPlaying = g.mode == Mode::Play;
    if (wasPlaying) teardownPlayRuntime();   // keep GameInstance vars, don't restore the old scene
    if (!g.scene.deserialize(data)) { addLog(2, "Open Level: invalid file: %s", abs.c_str()); return; }
    clearSceneHistory();
    g.navigation.baked = false;
    g.navigation.cells.clear();
    snprintf(g.projectPath, MAX_PATH, "%s", abs.c_str());
    g.selectedId = 0;
    g.selectedIds.clear();
    addLog(1, "Open Level: %s", req.c_str());
    if (wasPlaying) play();   // re-enter Play in the freshly loaded level
}

// ═══ interaction ═══
static void shootBall() {
    if (g.mode != Mode::Play || g.shots > 150) return;
    g.shots++;
    Vec3 origin, dir;
    g.camera.screenRay(0, 0, origin, dir);   // centre of the viewport
    char nm[48];
    snprintf(nm, sizeof(nm), "Projectile %d", g.shots);
    Entity& e = g.scene.spawnSphere(nm, origin + dir * 1.2f, 0.7f,
                                    { 0.95f, 0.55f + (rand() % 30) / 100.0f, 0.15f }, 3, 0.4f, 0.4f);
    e.body->velocity = dir * 18.0f;
}

static void beginGrab() {
    Vec3 origin, dir;
    mouseRay(origin, dir);
    RayHit hit;
    if (g.scene.world.raycast(origin, dir, 300, hit) && hit.body->type == BodyType::Dynamic && hit.body->enabled) {
        g.grabBody = hit.body;
        g.grabLocal = hit.body->quat.conjugate().rotate(hit.point - hit.body->position);
        g.grabDist = hit.t;
        g.grabTarget = hit.point;
        hit.body->wake();
    }
}

static void updateGrabTarget() {
    if (!g.grabBody) return;
    Vec3 origin, dir;
    mouseRay(origin, dir);
    g.grabTarget = origin + dir * g.grabDist;
}

static void applyGrabForce() {
    if (!g.grabBody) return;
    RigidBody* b = g.grabBody;
    Vec3 anchor = b->position + b->quat.rotate(g.grabLocal);
    Vec3 velAt = b->velocity + b->angularVelocity.cross(anchor - b->position);
    Vec3 f = (g.grabTarget - anchor) * (40.0f * b->mass) - velAt * (8.0f * b->mass);
    float maxF = 400.0f * b->mass;
    if (f.length() > maxF) f = f.normalized() * maxF;
    b->applyForceAt(f, anchor);
}

static void pickEntity() {
    Vec3 origin, dir;
    mouseRay(origin, dir);
    RayHit hit;
    if (g.scene.world.raycast(origin, dir, 500, hit)) {
        Entity* e = g.scene.byBody(hit.body);
        g.selectedId = e ? e->id : 0;
    } else {
        g.selectedId = 0;
    }
    g.selectedIds.clear();if(g.selectedId)g.selectedIds.insert(g.selectedId);
    g.outlinerSelectionAnchor=g.selectedId;
}

static void viewportMouseDown() {
    if (g.mode == Mode::Play) {
        beginGrab();
        return;
    }
    Entity* sel = g.scene.byId(g.selectedId);
    if (sel && g.showGizmo) {
        Vec3 origin, dir;
        mouseRay(origin, dir);
        int axis = gizmoHitTest(origin, dir, sel->body->position, sel->body->quat);
        if (axis >= 0) {
            g.gizmoAxis = axis;
            g.gizmoStartPos = sel->body->position;
            g.gizmoStartQuat = sel->body->quat;
            g.gizmoStartScale = sel->scale;
            g.gizmoDragAxis = gizmoAxisFor(sel->body->quat, axis);
            g.gizmoStartAIBaseOffset = sel->aiBaseOffset;
            g.gizmoStartT = axisParam(origin, dir, sel->body->position, g.gizmoDragAxis);
            g.gizmoRotationDeltaDeg=0;
            if(g.gizmoMode==1){
                Vec3 start;
                if(rayPlaneVector(origin,dir,sel->body->position,g.gizmoDragAxis,start))
                    g.gizmoRotationStartVector=start;
                else g.gizmoRotationStartVector=gizmoAxisFor(sel->body->quat,(axis+1)%3);
            }
            return;
        }
    }
    pickEntity();
}

static void viewportMouseMove() {
    if (g.gizmoAxis >= 0) {
        Entity* sel = g.scene.byId(g.selectedId);
        if (sel) {
            Vec3 origin, dir;
            mouseRay(origin, dir);
            float t = axisParam(origin, dir, g.gizmoStartPos, g.gizmoDragAxis);
            float delta = clampf(t - g.gizmoStartT, -400, 400);
            const Vec3& ax = g.gizmoDragAxis;
            if (g.gizmoMode == 1) {
                Vec3 current;
                if(!rayPlaneVector(origin,dir,g.gizmoStartPos,ax,current))return;
                float ang=atan2f(ax.dot(g.gizmoRotationStartVector.cross(current)),
                                 clampf(g.gizmoRotationStartVector.dot(current),-1,1));
                float degrees=ang/DEG2RAD;
                if(g.transformSnap&&g.rotateSnap>0)degrees=roundf(degrees/g.rotateSnap)*g.rotateSnap;
                g.gizmoRotationDeltaDeg=degrees;ang=degrees*DEG2RAD;
                Quat oldRotation = sel->body->quat;
                Quat newRotation = (Quat::axisAngle(ax, ang) * g.gizmoStartQuat).normalized();
                sel->body->quat = newRotation;
                sel->body->angularVelocity = {};
                sel->body->updateAABB();
                g.scene.rotateDescendants(sel->id, sel->body->position, oldRotation, newRotation);
                if ((sel->staticFlags & STATIC_NAVIGATION) || sel->hasNavigationOccluder) invalidateNavigation();
            } else if (g.gizmoMode == 2) {
                // scala lungo l'asse selezionato (componente locale)
                Vec3 s = g.gizmoStartScale;
                if (g.gizmoLocal) {
                    float& comp = g.gizmoAxis == 0 ? s.x : g.gizmoAxis == 1 ? s.y : s.z;
                    float amount=g.transformSnap&&g.scaleSnap>0?roundf(delta/g.scaleSnap)*g.scaleSnap:delta;
                    comp = clampf(comp + amount, 0.05f, 1000.0f);
                } else {
                    if(g.transformSnap&&g.scaleSnap>0)delta=roundf(delta/g.scaleSnap)*g.scaleSnap;
                    Vec3 localX=g.gizmoStartQuat.rotate(GIZMO_AXES[0]);
                    Vec3 localY=g.gizmoStartQuat.rotate(GIZMO_AXES[1]);
                    Vec3 localZ=g.gizmoStartQuat.rotate(GIZMO_AXES[2]);
                    s.x=clampf(s.x+delta*fabsf(localX.dot(ax)),.05f,1000.0f);
                    s.y=clampf(s.y+delta*fabsf(localY.dot(ax)),.05f,1000.0f);
                    s.z=clampf(s.z+delta*fabsf(localZ.dot(ax)),.05f,1000.0f);
                }
                Vec3 oldScale = sel->scale;
                sel->scale = s;
                g.scene.scaleDescendants(sel->id, sel->body->position, sel->body->quat, oldScale, s);
                g.scene.syncBodyShape(*sel);
                if ((sel->staticFlags & STATIC_NAVIGATION) || sel->hasNavigationOccluder) invalidateNavigation();
            } else {
                // sposta lungo l'asse
                if(g.transformSnap&&g.moveSnap>0)delta=roundf(delta/g.moveSnap)*g.moveSnap;
                Vec3 newPos = g.gizmoStartPos + ax * delta;
                Vec3 d = newPos - sel->body->position;
                sel->body->position = newPos;
                sel->body->velocity = {};
                sel->body->angularVelocity = {};
                sel->body->updateAABB();
                g.scene.moveDescendants(sel->id, d);
                if ((sel->staticFlags & STATIC_NAVIGATION) || sel->hasNavigationOccluder) invalidateNavigation();
            }
        }
        return;
    }
    if (g.mode == Mode::Edit && g.showGizmo && !g.orbiting && !g.panning && mouseInViewport() && !g.ui.wantMouse()) {
        Entity* sel = g.scene.byId(g.selectedId);
        if (sel) {
            Vec3 origin, dir;
            mouseRay(origin, dir);
            g.hoverAxis = gizmoHitTest(origin, dir, sel->body->position, sel->body->quat);
        } else {
            g.hoverAxis = -1;
        }
    }
    updateGrabTarget();
}

// true when an interactive element of a widget on the viewport is under the
// cursor — such a click belongs to the UI, not to the game view
static bool playWidgetUnderCursor() {
    if (g.mode != Mode::Play || g.activeDoc != 0) return false;
    UIRect vp = playWidgetRect();
    for (auto& rw : g.runtimeWidgets)
        if (rw->visible && widgetNodeAtPoint(rw->asset, vp, (float)g.mouseX, (float)g.mouseY)) return true;
    return false;
}

// ── runtime widgets: per-step tick and pointer events ──
// Handles are collected first: a graph may add or remove widgets while it runs.
static std::vector<int> visibleWidgetHandles() {
    std::vector<int> handles;
    for (auto& rw : g.runtimeWidgets) if (rw->visible) handles.push_back(rw->handle);
    return handles;
}

// Event Tick on every widget on the viewport. This is also what advances a
// widget graph's latent nodes (Delay, timers), so it runs even with no Tick node.
static void widgetsTick() {
    for (int handle : visibleWidgetHandles()) widgetFire(handle, BP_EV_W_TICK);
}

// Pointer events, once per frame (mousePressed/Released are per-frame edges).
// Only while the Play cursor is free — with FPS mouse-look the OS cursor is
// recentred every frame, so its position means nothing. Shift+F1 frees it.
static void widgetsPointer() {
    bool cursorUsable = !g.playMouseCaptured && g.activeDoc == 0 && mouseInViewport() && !g.ui.wantMouse();
    UIRect vp = playWidgetRect();
    float mx = (float)g.mouseX, my = (float)g.mouseY;
    // topmost widget first: the one added last is drawn on top and takes the pointer
    std::vector<int> handles = visibleWidgetHandles();
    int ownerHandle = 0;
    std::string ownerElement;
    if (cursorUsable) {
        for (auto it = handles.rbegin(); it != handles.rend() && !ownerHandle; ++it) {
            RuntimeWidget* rw = runtimeWidgetByHandle(*it);
            if (!rw) continue;
            if (const WidgetNode* n = widgetNodeAtPoint(rw->asset, vp, mx, my)) {
                ownerHandle = rw->handle;
                ownerElement = n->name;
            }
        }
    }
    for (int handle : handles) {
        RuntimeWidget* rw = runtimeWidgetByHandle(handle);
        if (!rw) continue;
        std::string element = handle == ownerHandle ? ownerElement : std::string();
        std::string wasHover = rw->hoverElement;
        if (element != wasHover) {
            rw->hoverElement = element;
            if (!wasHover.empty()) widgetFire(handle, BP_EV_W_MOUSE_LEAVE, wasHover.c_str());
            rw = runtimeWidgetByHandle(handle);
            if (!rw) continue;
            if (!element.empty()) widgetFire(handle, BP_EV_W_MOUSE_ENTER, element.c_str());
        }
        if (g.uiIn.mousePressed && !element.empty()) {
            rw = runtimeWidgetByHandle(handle);
            if (rw) rw->pressElement = element;
            widgetFire(handle, BP_EV_W_MOUSE_DOWN, element.c_str());
        }
        if (g.uiIn.mouseReleased) {
            rw = runtimeWidgetByHandle(handle);
            if (!rw || rw->pressElement.empty()) continue;   // no press started here
            // release reports the element the press started on, so a click that
            // slides off the button still tells the graph which one it was
            std::string pressed = rw->pressElement;
            rw->pressElement.clear();
            widgetFire(handle, BP_EV_W_MOUSE_UP, pressed.c_str());
        }
    }
}

// ═══ simulation ═══
static void stepSim(float dt) {
    // input events, once per frame: Started (press), Completed (release),
    // Triggered (every frame while the key is held) — Unreal input-action style
    for (int k : g.bpKeyEvents) bpFireAll(BP_EV_KEY, 0, k, -1, 0, 1);
    g.bpKeyEvents.clear();
    for (int k : g.bpKeyReleases) bpFireAll(BP_EV_KEY, 0, k, -1, 0, 2);
    g.bpKeyReleases.clear();
    for (int k = 0; k < BP_NKEYS; k++) {
        if (g.bpKeysDown[k]) bpFireAll(BP_EV_KEY, 0, k, -1, 0, 0);
    }

    // axis values for this frame (mouse deltas + key pairs), then fire InputAxis
    // key indices in BP_KEY_VKS: W=1 A=2 S=3 D=4 SU=7 GIU=8 SX=9 DX=10
    g.bpAxisValues[0] = g.bpMouseDX;
    g.bpAxisValues[1] = -g.bpMouseDY;   // in su = positivo
    g.bpAxisValues[2] = g.bpWheelAccum;
    g.bpAxisValues[3] = (g.bpKeysDown[4] ? 1.0f : 0.0f) - (g.bpKeysDown[2] ? 1.0f : 0.0f);
    g.bpAxisValues[4] = (g.bpKeysDown[1] ? 1.0f : 0.0f) - (g.bpKeysDown[3] ? 1.0f : 0.0f);
    g.bpAxisValues[5] = (g.bpKeysDown[10] ? 1.0f : 0.0f) - (g.bpKeysDown[9] ? 1.0f : 0.0f);
    g.bpAxisValues[6] = (g.bpKeysDown[7] ? 1.0f : 0.0f) - (g.bpKeysDown[8] ? 1.0f : 0.0f);
    bpFireAll(BP_EV_AXIS);

    // InputAction bound to Mouse X / Y / XY (choice = BP_NKEYS + i):
    // Triggered every frame, Started when movement begins, Completed when it stops
    for (int i = 0; i < 3; i++) {
        bool nz = i == 0 ? g.bpAxisValues[0] != 0
                : i == 1 ? g.bpAxisValues[1] != 0
                         : (g.bpAxisValues[0] != 0 || g.bpAxisValues[1] != 0);
        if (nz && !g.bpBindAxisActive[i]) bpFireAll(BP_EV_KEY, 0, BP_NKEYS + i, -1, 0, 1);
        bpFireAll(BP_EV_KEY, 0, BP_NKEYS + i, -1, 0, 0);
        if (!nz && g.bpBindAxisActive[i]) bpFireAll(BP_EV_KEY, 0, BP_NKEYS + i, -1, 0, 2);
        g.bpBindAxisActive[i] = nz;
    }
    widgetsPointer();   // once per frame: hover/press/release are per-frame edges

    g.accumulator += dt;
    int steps = 0;
    while (g.accumulator >= FIXED_DT && steps < 4) {
        applyGrabForce();
        for (auto& e : g.scene.entities) {
            if (e.behavior == BH_SPIN && e.body->type == BodyType::Dynamic) {
                e.body->applyTorque({ 0, e.bp[0], 0 });
            }
            if (e.behavior == BH_JUMP_SPACE && g.spaceQueued && e.body->type == BodyType::Dynamic) {
                e.body->applyImpulse({ 0, e.bp[0] * e.body->mass, 0 });
            }
        }
        g.spaceQueued = false;
        bpFireAll(BP_EV_TICK);
        widgetsTick();
        updateAIAgents(FIXED_DT);
        g.scene.world.step(FIXED_DT);
        propagateAttachments();   // camera/trigger/figli senza fisica seguono il padre
        updateAnimators(FIXED_DT);
        g.playTime += FIXED_DT;
        // collision events → blueprint (with the other entity id)
        for (const auto& ce : g.scene.world.contactEvents) {
            Entity* ea = g.scene.byBody(ce.a);
            Entity* eb = g.scene.byBody(ce.b);
            if (ea) bpFireAll(BP_EV_HIT, ce.impulse, -1, ea->id, eb ? eb->id : 0);
            if (eb) bpFireAll(BP_EV_HIT, ce.impulse, -1, eb->id, ea ? ea->id : 0);
        }
        for (const auto& overlap : g.scene.world.overlapEvents) {
            Entity* ea = g.scene.byBody(overlap.a);
            Entity* eb = g.scene.byBody(overlap.b);
            int eventType = overlap.begin ? BP_EV_BEGIN_OVERLAP : BP_EV_END_OVERLAP;
            if (ea) {
                bpFireAll(eventType, 0, -1, ea->id, eb ? eb->id : 0);
                bpFireOverlapBindings(overlap.begin, ea->id, eb ? eb->id : 0);
            }
            if (eb) {
                bpFireAll(eventType, 0, -1, eb->id, ea ? ea->id : 0);
                bpFireOverlapBindings(overlap.begin, eb->id, ea ? ea->id : 0);
            }
        }
        // joint breaking → remove the broken joints from the scene
        {
            std::vector<int> brokenJoints;
            for (auto& c : g.scene.world.constraints) {
                if (c.broken && c.userIndex >= 0) brokenJoints.push_back(c.userIndex);
            }
            if (!brokenJoints.empty()) {
                std::sort(brokenJoints.rbegin(), brokenJoints.rend());
                for (int ji : brokenJoints) {
                    if (ji < (int)g.scene.joints.size()) {
                        addLog(1, "Constraint broken! (impulse above the threshold)");
                        g.scene.joints.erase(g.scene.joints.begin() + ji);
                    }
                }
                g.scene.rebuildConstraints();
            }
        }
        bpProcessDestroys();
        g.accumulator -= FIXED_DT;
        steps++;
        if (!g.pendingOpenLevel.empty()) break;   // Open Level requested: stop stepping the old scene
    }
    if (steps == 4) g.accumulator = 0;
    processPendingOpenLevel();   // safe point: swap scenes after the sim step(s)
    // consumed: Tick chains later this frame still read the same values
    g.bpMouseDX = 0;
    g.bpMouseDY = 0;
    g.bpWheelAccum = 0;
}

// ═══ 3D frame ═══
// Wire representation of the actual primitive/collider. Used for selected
// gizmos and invisible physics volumes, never a generic bounding cube.
static void appendShapeWire(std::vector<LineVert>& lines, const Entity& e, const Vec3& color) {
    MeshType wireMesh = e.hasTrigger
                      ? (e.triggerShape == 1 ? MESH_SPHERE : e.triggerShape == 2 ? MESH_CAPSULE : MESH_CUBE)
                      : e.mesh;
    auto world = [&](Vec3 p) {
        p = { p.x * e.scale.x, p.y * e.scale.y, p.z * e.scale.z };
        return e.body->quat.rotate(p) + e.body->position;
    };
    auto line = [&](Vec3 a, Vec3 b) {
        lines.push_back({ world(a), color });
        lines.push_back({ world(b), color });
    };
    const int SEG = 20;
    if (wireMesh == MESH_CUBE) {
        Vec3 c[8];
        for (int i = 0; i < 8; i++) c[i] = { (i & 1) ? .5f : -.5f, (i & 2) ? .5f : -.5f, (i & 4) ? .5f : -.5f };
        const int edges[12][2] = { {0,1},{1,3},{3,2},{2,0},{4,5},{5,7},{7,6},{6,4},{0,4},{1,5},{2,6},{3,7} };
        for (const auto& edge : edges) line(c[edge[0]], c[edge[1]]);
    } else if (wireMesh == MESH_SPHERE) {
        for (int ring = 0; ring < 3; ring++) for (int i = 0; i < SEG; i++) {
            float a = 6.28318530718f * i / SEG, b = 6.28318530718f * (i + 1) / SEG;
            Vec3 p, q;
            if (ring == 0) { p = { cosf(a)*.5f, sinf(a)*.5f, 0 }; q = { cosf(b)*.5f, sinf(b)*.5f, 0 }; }
            else if (ring == 1) { p = { cosf(a)*.5f, 0, sinf(a)*.5f }; q = { cosf(b)*.5f, 0, sinf(b)*.5f }; }
            else { p = { 0, cosf(a)*.5f, sinf(a)*.5f }; q = { 0, cosf(b)*.5f, sinf(b)*.5f }; }
            line(p, q);
        }
    } else if (wireMesh == MESH_CYLINDER || wireMesh == MESH_CONE) {
        for (int i = 0; i < SEG; i++) {
            float a = 6.28318530718f * i / SEG, b = 6.28318530718f * (i + 1) / SEG;
            Vec3 p0 = { cosf(a)*.5f, -.5f, sinf(a)*.5f }, p1 = { cosf(b)*.5f, -.5f, sinf(b)*.5f };
            line(p0, p1);
            if (wireMesh == MESH_CYLINDER) {
                Vec3 q0 = { p0.x, .5f, p0.z }, q1 = { p1.x, .5f, p1.z };
                line(q0, q1);
                if (i % 5 == 0) line(p0, q0);
            } else if (i % 5 == 0) line(p0, { 0, .5f, 0 });
        }
    } else if (wireMesh == MESH_CAPSULE) {
        for (int i = 0; i < SEG; i++) {
            float a = 6.28318530718f * i / SEG, b = 6.28318530718f * (i + 1) / SEG;
            line({ cosf(a)*.5f, -.5f, sinf(a)*.5f }, { cosf(b)*.5f, -.5f, sinf(b)*.5f });
            line({ cosf(a)*.5f,  .5f, sinf(a)*.5f }, { cosf(b)*.5f,  .5f, sinf(b)*.5f });
            if (i % 5 == 0) line({ cosf(a)*.5f, -.5f, sinf(a)*.5f }, { cosf(a)*.5f, .5f, sinf(a)*.5f });
        }
        for (int plane = 0; plane < 2; plane++) for (int i = 0; i < SEG / 2; i++) {
            float a = 3.14159265359f * i / (SEG / 2), b = 3.14159265359f * (i + 1) / (SEG / 2);
            Vec3 ta = plane == 0 ? Vec3{ cosf(a)*.5f, .5f + sinf(a)*.5f, 0 } : Vec3{ 0, .5f + sinf(a)*.5f, cosf(a)*.5f };
            Vec3 tb = plane == 0 ? Vec3{ cosf(b)*.5f, .5f + sinf(b)*.5f, 0 } : Vec3{ 0, .5f + sinf(b)*.5f, cosf(b)*.5f };
            Vec3 ba = plane == 0 ? Vec3{ cosf(a)*.5f, -.5f - sinf(a)*.5f, 0 } : Vec3{ 0, -.5f - sinf(a)*.5f, cosf(a)*.5f };
            Vec3 bb = plane == 0 ? Vec3{ cosf(b)*.5f, -.5f - sinf(b)*.5f, 0 } : Vec3{ 0, -.5f - sinf(b)*.5f, cosf(b)*.5f };
            line(ta, tb); line(ba, bb);
        }
    }
}

static void appendAudioSourceGizmo(std::vector<LineVert>& lines, const Entity& e, bool selected) {
    if (!e.body) return;
    auto world = [&](const Vec3& p) { return e.body->position + e.body->quat.rotate(p); };
    auto localLine = [&](const Vec3& a, const Vec3& b, const Vec3& color = Vec3{ 0.95f, 0.45f, 0.82f }) {
        lines.push_back({ world(a), color }); lines.push_back({ world(b), color });
    };

    // Small directional speaker icon. It remains a fixed world size and is
    // independent of the rendered mesh/collider scale.
    Vec3 back[4] = { { -.12f,-.12f,-.16f }, { .12f,-.12f,-.16f }, { .12f,.12f,-.16f }, { -.12f,.12f,-.16f } };
    Vec3 front[4] = { { -.21f,-.21f,.10f }, { .21f,-.21f,.10f }, { .21f,.21f,.10f }, { -.21f,.21f,.10f } };
    for (int i = 0; i < 4; i++) {
        localLine(back[i], back[(i + 1) % 4]);
        localLine(front[i], front[(i + 1) % 4]);
        localLine(back[i], front[i]);
    }
    const int ARC = 12;
    for (int ring = 0; ring < 3; ring++) {
        float radius = .32f + ring * .17f;
        for (int i = 0; i < ARC; i++) {
            float a = -1.0f + 2.0f * i / ARC, b = -1.0f + 2.0f * (i + 1) / ARC;
            localLine({ sinf(a) * radius, 0, .10f + cosf(a) * radius },
                      { sinf(b) * radius, 0, .10f + cosf(b) * radius });
            localLine({ 0, sinf(a) * radius, .10f + cosf(a) * radius },
                      { 0, sinf(b) * radius, .10f + cosf(b) * radius });
        }
    }

    // Spatial falloff is shown only for the selected source to avoid filling
    // the whole scene with large distance spheres.
    const AudioAttenuationAsset* settings = audioAttenuationAsset(e.audioAttenuation);
    bool spatial = settings ? settings->spatial : e.audioSpatial;
    float minDistance = settings ? settings->minDistance : e.audioMinDistance;
    float maxDistance = settings ? settings->maxDistance : e.audioMaxDistance;
    if (!selected || !spatial) return;
    auto circle = [&](float radius, int plane, const Vec3& color) {
        if (radius <= .001f) return;
        const int SEG = 40;
        for (int i = 0; i < SEG; i++) {
            float a = 6.28318530718f * i / SEG, b = 6.28318530718f * (i + 1) / SEG;
            Vec3 p, q;
            if (plane == 0) { p = { cosf(a)*radius, sinf(a)*radius, 0 }; q = { cosf(b)*radius, sinf(b)*radius, 0 }; }
            else if (plane == 1) { p = { cosf(a)*radius, 0, sinf(a)*radius }; q = { cosf(b)*radius, 0, sinf(b)*radius }; }
            else { p = { 0, cosf(a)*radius, sinf(a)*radius }; q = { 0, cosf(b)*radius, sinf(b)*radius }; }
            // Attenuation spheres are world-aligned around the source.
            lines.push_back({ e.body->position + p, color }); lines.push_back({ e.body->position + q, color });
        }
    };
    for (int plane = 0; plane < 3; plane++) {
        circle(minDistance, plane, { .92f, .55f, .82f });
        circle(maxDistance, plane, { .48f, .30f, .58f });
    }
}

static void appendWorldSphere(std::vector<LineVert>& lines, const Vec3& center, float radius, const Vec3& color) {
    const int segments = 40;
    if (radius <= 0.001f) return;
    for (int plane = 0; plane < 3; plane++) for (int i = 0; i < segments; i++) {
        float a = 2.0f * PI * i / segments, b = 2.0f * PI * (i + 1) / segments;
        Vec3 p, q;
        if (plane == 0) { p = { cosf(a)*radius, sinf(a)*radius, 0 }; q = { cosf(b)*radius, sinf(b)*radius, 0 }; }
        else if (plane == 1) { p = { cosf(a)*radius, 0, sinf(a)*radius }; q = { cosf(b)*radius, 0, sinf(b)*radius }; }
        else { p = { 0, cosf(a)*radius, sinf(a)*radius }; q = { 0, cosf(b)*radius, sinf(b)*radius }; }
        lines.push_back({ center + p, color }); lines.push_back({ center + q, color });
    }
}

static bool sceneEntitySelected(int id) {
    // selectedId is the active object (and owns the transform gizmo), while
    // selectedIds contains the complete Outliner multi-selection.
    return id == g.selectedId || g.selectedIds.count(id) != 0;
}

static int appendSceneSelectionWires(std::vector<LineVert>& lines) {
    int highlighted = 0;
    for (const Entity& e : g.scene.entities) {
        if (!sceneEntitySelected(e.id)) continue;
        if (e.hasMesh || (e.hasTrigger && g.showColliders)) {
            appendShapeWire(lines, e, { 1, 0.8f, 0.2f });
            highlighted++;
        }
    }
    return highlighted;
}

// resolve a material asset (rel path) into shader params + albedo texture. Uses the
// live editor version if the material is open in a tab, else a parsed-file cache.
static void resolveMaterial(const std::string& rel, MaterialEval& ev, GLuint& tex) {
    for (auto& m : g.materialDocs) {
        if (m && m->curPath == rel) { ev = m->material.evaluate(); tex = ev.baseColorTex.empty() ? 0 : matLoadTexture(&g.renderer, g.projectDir, ev.baseColorTex); return; }
    }
    auto it = g.materialCache.find(rel);
    if (it == g.materialCache.end()) {
        MaterialAsset a; std::string data;
        if (readFile(g.projectDir + "\\" + rel, data)) a.deserialize(data);
        it = g.materialCache.emplace(rel, a).first;
    }
    ev = it->second.evaluate();
    tex = ev.baseColorTex.empty() ? 0 : matLoadTexture(&g.renderer, g.projectDir, ev.baseColorTex);
}

static void buildFrame(Frame& f) {
    // A Frame instance is reused by the main loop. Reset the environment so
    // temporary editor workspaces (such as Prefab Mode) cannot leak their
    // neutral sky/fog/light settings back into the level view.
    f.env = Env{};
    f.items.clear();
    f.overlay.clear();
    f.lights.clear();
    f.linesDepth.clear();
    f.trianglesDepth.clear();
    f.linesOverlay.clear();
    f.linesOverlayThick.clear();
    f.shadowCenter = g.camera.target;

    float az = g.sunAzimuth * DEG2RAD, el = g.sunElevation * DEG2RAD;
    f.env.sunDir = Vec3{ cosf(el) * cosf(az), sinf(el), cosf(el) * sinf(az) }.normalized();
    f.env.sunColor = Vec3{ 1, 0.96f, 0.88f } * g.sunIntensity;
    f.env.fogDensity = g.fogDensity;
    f.env.shadowStrength = g.shadowStrength;
    f.showGrid = g.showGrid && g.mode == Mode::Edit;   // debug hidden in Play
    if(g.prefabEditMode){
        // Prefab Mode is an isolated local workspace, not a level: use a
        // uniform neutral studio background and hide the world floor grid.
        const Vec3 neutral={.115f,.125f,.145f};
        f.env.horizon=neutral;f.env.zenith=neutral;f.env.fogColor=neutral;
        f.env.fogDensity=0;f.env.ambientSky={.30f,.30f,.32f};f.env.ambientGround={.20f,.20f,.22f};
        f.env.sunColor={.82f,.82f,.82f};f.env.shadowStrength=.55f;f.showGrid=false;
    }

    uint32_t cameraMask = 0xFFFFFFFFu;
    if (g.mode == Mode::Play && !g.flyActive)
        for (const Entity& cameraEntity : g.scene.entities)
            if (cameraEntity.isCamera) { cameraMask = cameraEntity.camLayerMask; break; }
    for (const auto& e : g.scene.entities) {
        int renderLayer = (std::max)(0, (std::min)(31, e.layer));
        bool layerVisible = (cameraMask & (1u << renderLayer)) != 0;
        if (e.hasMesh && layerVisible) {
            DrawItem it;
            it.mesh = e.mesh;
            it.model = Mat4::compose(e.body->position, e.body->quat, e.scale);
            it.color = (g.mode == Mode::Play && e.body->sleeping) ? e.color * 0.72f : e.color;
            it.opacity = e.colorAlpha;
            it.shininess = e.shininess;
            it.specular = e.specular;
            it.checker = e.checker;
            it.emissive = e.emissive;
            it.doubleSided = e.doubleSided;
            if (e.materialAsset[0]) {   // assigned material overrides the inline surface params
                MaterialEval ev; GLuint tex = 0;
                resolveMaterial(e.materialAsset, ev, tex);
                applyMaterialEval(ev, it.color, it.specular, it.shininess, it.emissive);
                if (g.mode == Mode::Play && e.body->sleeping) it.color = it.color * 0.72f;
                it.albedoTex = tex;
            }
            f.items.push_back(it);
        }
        if (e.isLight && layerVisible) {
            f.lights.push_back({ e.body->position, e.lightColor * e.lightIntensity, e.lightRange });
        }
        // invisible physics volumes (triggers) and cameras: wire gizmos in Edit mode
        if (!g.prefabEditMode && g.mode == Mode::Edit && g.showColliders && e.hasTrigger)
            appendShapeWire(f.linesDepth, e, { 0.35f, 0.9f, 0.5f });
        if (!g.prefabEditMode && g.mode == Mode::Edit && g.showGizmo && e.hasNavigationOccluder)
            appendShapeWire(f.linesDepth, e, { 1.0f, 0.48f, 0.12f });
        if (!g.prefabEditMode && g.mode == Mode::Edit && g.showGizmo && e.isCamera) {
            // small frustum: eye point + 4 corner rays
            Vec3 eye = e.body->position + Vec3{ 0, e.camOffsetY, 0 };
            const Vec3 cyan = { 0.4f, 0.85f, 0.95f };
            for (int i = 0; i < 4; i++) {
                Vec3 c = { (i & 1) ? 0.5f : -0.5f, (i & 2) ? 0.35f : -0.35f, -1.1f };
                Vec3 p = e.body->quat.rotate(c) + eye;
                f.linesDepth.push_back({ eye, cyan });
                f.linesDepth.push_back({ p, cyan });
            }
        }
        if (!g.prefabEditMode && g.mode == Mode::Edit && g.showGizmo && e.hasAudio)
            appendAudioSourceGizmo(f.linesDepth, e, sceneEntitySelected(e.id));
        if (!g.prefabEditMode && g.mode == Mode::Edit && g.showGizmo && e.hasReverb)
            appendWorldSphere(f.linesDepth, e.body->position, e.reverbRadius,
                              sceneEntitySelected(e.id) ? Vec3{ .82f, .38f, 1.0f } : Vec3{ .42f, .20f, .55f });
        if (!g.prefabEditMode && g.navigation.show && g.showGizmo && e.hasAIAgent && e.aiDebugDraw && e.aiPath.size() > 1) {
            Vec3 previous = e.body->position;
            for (int i = e.aiPathIndex; i < (int)e.aiPath.size(); i++) {
                f.linesOverlay.push_back({ previous, { .20f, .88f, 1.0f } });
                f.linesOverlay.push_back({ e.aiPath[i], { .20f, .88f, 1.0f } });
                previous = e.aiPath[i];
            }
        }
    }

    if (!g.prefabEditMode && g.navigation.baked && g.navigation.show && g.mode == Mode::Edit) {
        float half = g.navigation.cellSize * 0.5f;
        const Vec3 blue{ .10f, .52f, .98f };
        const Vec3 edge{ .42f, .84f, 1.0f };   // crisp, opaque outline of the walkable region
        for (int z = 0; z < g.navigation.height; z++) for (int x = 0; x < g.navigation.width; x++) {
            if (!navWalkable(x, z)) continue;
            Vec3 p = navCellPosition(navIndex(x, z)); p.y += 0.035f;
            Vec3 a=p+Vec3{-half,0,-half}, b=p+Vec3{half,0,-half}, c=p+Vec3{half,0,half}, d=p+Vec3{-half,0,half};
            f.trianglesDepth.push_back({a,blue}); f.trianglesDepth.push_back({b,blue}); f.trianglesDepth.push_back({c,blue});
            f.trianglesDepth.push_back({a,blue}); f.trianglesDepth.push_back({c,blue}); f.trianglesDepth.push_back({d,blue});
            // Outline the region: any cell edge that borders a hole or the mesh boundary
            // is drawn as an opaque depth-tested line, so the navmesh reads with a clear,
            // Unreal-style border instead of only a faint translucent fill.
            Vec3 ea=a,eb=b,ec=c,ed=d; ea.y=eb.y=ec.y=ed.y=p.y+0.02f;
            if(!navWalkable(x,z-1)){f.linesDepth.push_back({ea,edge});f.linesDepth.push_back({eb,edge});}
            if(!navWalkable(x+1,z)){f.linesDepth.push_back({eb,edge});f.linesDepth.push_back({ec,edge});}
            if(!navWalkable(x,z+1)){f.linesDepth.push_back({ec,edge});f.linesDepth.push_back({ed,edge});}
            if(!navWalkable(x-1,z)){f.linesDepth.push_back({ed,edge});f.linesDepth.push_back({ea,edge});}
        }
    }

    if(!g.prefabEditMode){
        for (const auto& c : g.scene.world.constraints) {
            f.linesDepth.push_back({ c.a->position, { 0.4f, 0.9f, 0.95f } });
            f.linesDepth.push_back({ c.b->position, { 0.4f, 0.9f, 0.95f } });
        }
        // Physics Constraint debug gizmos (drawn on top, always visible): green
        // marker/line to Object 1, red to Object 2, and a yellow line between the
        // two so which object is 1 vs 2 is unambiguous (Unreal-style).
        if (g.showGizmo) for (const auto& e : g.scene.entities) {
            if (!e.hasConstraint || !e.body) continue;
            Vec3 c = e.body->position;
            const Vec3 green{ .30f, .95f, .40f }, red{ 1.0f, .35f, .30f }, mid{ .95f, .85f, .35f };
            Vec3 dx{ .3f, 0, 0 }, dy{ 0, .3f, 0 }, dz{ 0, 0, .3f };   // small cross at the constraint
            f.linesOverlay.push_back({ c - dx, mid }); f.linesOverlay.push_back({ c + dx, mid });
            f.linesOverlay.push_back({ c - dy, mid }); f.linesOverlay.push_back({ c + dy, mid });
            f.linesOverlay.push_back({ c - dz, mid }); f.linesOverlay.push_back({ c + dz, mid });
            Entity* a = g.scene.byId(e.constraintObjA);
            Entity* b = g.scene.byId(e.constraintObjB);
            if (a && a->body) { f.linesOverlay.push_back({ c, green }); f.linesOverlay.push_back({ a->body->position, green });
                                appendWorldSphere(f.linesOverlay, a->body->position, 0.35f, green); }
            if (b && b->body) { f.linesOverlay.push_back({ c, red }); f.linesOverlay.push_back({ b->body->position, red });
                                appendWorldSphere(f.linesOverlay, b->body->position, 0.35f, red); }
            if (a && a->body && b && b->body) { f.linesOverlay.push_back({ a->body->position, mid });
                                                f.linesOverlay.push_back({ b->body->position, mid }); }
        }
        for (const auto& d : g.debugSegs) {   // trace debug (Unreal-style)
            f.linesDepth.push_back({ d.a, d.color });
            f.linesDepth.push_back({ d.b, d.color });
        }
    }

    if (!g.prefabEditMode && g.showContacts && g.mode == Mode::Play) {
        g.scene.world.eachManifold([&](const Manifold& m) {
            for (int i = 0; i < m.numPoints; i++) {
                Vec3 p = m.a->position + m.points[i].rA;
                const float s = 0.07f;
                const Vec3 red = { 1, 0.25f, 0.25f };
                f.linesOverlay.push_back({ { p.x - s, p.y, p.z }, red });
                f.linesOverlay.push_back({ { p.x + s, p.y, p.z }, red });
                f.linesOverlay.push_back({ { p.x, p.y - s, p.z }, red });
                f.linesOverlay.push_back({ { p.x, p.y + s, p.z }, red });
                f.linesOverlay.push_back({ { p.x, p.y, p.z - s }, red });
                f.linesOverlay.push_back({ { p.x, p.y, p.z + s }, red });
            }
        });
    }

    Entity* sel = g.scene.byId(g.selectedId);
    if (sel && g.mode == Mode::Edit && g.showGizmo) {
        appendSceneSelectionWires(f.linesOverlay);
        buildGizmoOverlay(f, sel->body->position, sel->body->quat);
    }

    if (g.grabBody) {
        Vec3 anchor = g.grabBody->position + g.grabBody->quat.rotate(g.grabLocal);
        f.linesOverlay.push_back({ anchor, { 1, 0.85f, 0.2f } });
        f.linesOverlay.push_back({ g.grabTarget, { 1, 0.85f, 0.2f } });
    }
}

// ═══ file management ═══
static bool fileDialog(bool save, const char* filter, const char* defExt, char* outPath, int cap,
                       const char* initial = nullptr, const char* initialDir = nullptr) {
    char file[MAX_PATH] = "";
    if (initial) strcpy(file, initial);
    OPENFILENAMEA ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = g.hwnd;
    ofn.lpstrFilter = filter;
    ofn.lpstrFile = file;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrDefExt = defExt;
    ofn.lpstrInitialDir = initialDir ? initialDir : g.projectDir.c_str();
    ofn.Flags = save ? (OFN_OVERWRITEPROMPT | OFN_NOCHANGEDIR) : (OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR);
    BOOL ok = save ? GetSaveFileNameA(&ofn) : GetOpenFileNameA(&ofn);
    if (!ok) return false;
    snprintf(outPath, cap, "%s", file);
    return true;
}

static bool writeFile(const std::string& path, const std::string& data) {
    FILE* f = fopen(path.c_str(), "wb");
    if (!f) return false;
    fwrite(data.data(), 1, data.size(), f);
    fclose(f);
    return true;
}

static bool readFile(const std::string& path, std::string& out) {
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) return false;
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    out.resize(size);
    fread(out.data(), 1, size, f);
    fclose(f);
    return true;
}

static bool openAudioSettingsAsset(const std::string& relativePath, int kind) {
    std::string data;
    if (!readFile(g.projectDir + "\\" + relativePath, data)) return false;
    bool ok = kind == 0 ? g.audioClassEdit.deserialize(data)
             : kind == 1 ? g.audioAttenuationEdit.deserialize(data)
                         : g.audioConcurrencyEdit.deserialize(data);
    if (!ok) return false;
    g.audioAssetEditKind = kind;
    g.audioAssetEditRel = relativePath;
    g.enumAssetEditRel.clear();
    g.selectedId = 0;
    return true;
}

static bool saveAudioSettingsAsset() {
    if (g.audioAssetEditKind < 0 || g.audioAssetEditRel.empty()) return false;
    std::string data = g.audioAssetEditKind == 0 ? g.audioClassEdit.serialize()
                     : g.audioAssetEditKind == 1 ? g.audioAttenuationEdit.serialize()
                                                 : g.audioConcurrencyEdit.serialize();
    if (!writeFile(g.projectDir + "\\" + g.audioAssetEditRel, data)) return false;
    if (g.audioAssetEditKind == 0) g.audioClassCache[g.audioAssetEditRel] = g.audioClassEdit;
    else if (g.audioAssetEditKind == 1) g.audioAttenuationCache[g.audioAssetEditRel] = g.audioAttenuationEdit;
    else g.audioConcurrencyCache[g.audioAssetEditRel] = g.audioConcurrencyEdit;
    return true;
}

static bool openEnumAsset(const std::string& relativePath) {
    std::string data;
    if (!readFile(g.projectDir + "\\" + relativePath, data) || !g.enumAssetEdit.deserialize(data)) return false;
    g.enumAssetEditRel = relativePath;
    g.audioAssetEditKind = -1;
    g.selectedId = 0;
    return true;
}

static bool saveEnumAsset() {
    return !g.enumAssetEditRel.empty() &&
           writeFile(g.projectDir + "\\" + g.enumAssetEditRel, g.enumAssetEdit.serialize());
}

static void saveProject(bool forceDialog) {
    if (g.mode == Mode::Play) { addLog(2, "Stop the simulation before saving."); return; }
    if(g.prefabEditMode){savePrefabEdit();return;}
    bool hadAnimationPreview = g.animationPreviewActive;
    float previewTime = g.animationTime;
    if (hadAnimationPreview) restoreAnimationPreview();
    if (forceDialog || !g.projectPath[0]) {
        if (!fileDialog(true, "Pulse Engine Scene (*.imp)\0*.imp\0All files (*.*)\0*.*\0", "imp", g.projectPath, MAX_PATH)) {
            if (hadAnimationPreview) applyAnimationPreview(previewTime);
            return;
        }
    }
    if (writeFile(g.projectPath, g.scene.serialize())) {
        addLog(1, "Scene saved: %s", g.projectPath);
        scanBrowser();
    } else {
        addLog(2, "Could not write: %s", g.projectPath);
    }
    if (hadAnimationPreview) applyAnimationPreview(previewTime);
}

static void openProjectFile(const std::string& path) {
    if (g.mode == Mode::Play) stopPlay();
    restoreAnimationPreview();
    g.animationPlaying = false;
    g.animationRecording = false;
    std::string data;
    if (!readFile(path, data)) { addLog(2, "Could not open: %s", path.c_str()); return; }
    if (g.scene.deserialize(data)) {
        clearSceneHistory();
        g.navigation.baked = false;
        g.navigation.cells.clear();
        g.navigation.status = "Scene loaded: run the Navigation Bake.";
        snprintf(g.projectPath, MAX_PATH, "%s", path.c_str());
        g.selectedId = 0;
        addLog(1, "Scene loaded: %s (%d objects)", path.c_str(), (int)g.scene.entities.size());
    } else {
        addLog(2, "Invalid file: %s", path.c_str());
    }
}

static void openProject() {
    char path[MAX_PATH];
    if (!fileDialog(false, "Pulse Engine Scene (*.imp)\0*.imp\0All files (*.*)\0*.*\0", "imp", path, MAX_PATH)) return;
    openProjectFile(path);
}

static void savePrefab() {
    Entity* sel = g.scene.byId(g.selectedId);
    if (!sel) { addLog(2, "Select an object to save as a prefab first."); return; }
    char path[MAX_PATH];
    char suggested[MAX_PATH];
    snprintf(suggested, sizeof(suggested), "%s.pfb", sel->name);
    for (char* c = suggested; *c; c++) if (*c == ' ') *c = '_';
    std::string dir = curDirAbs();
    if (!fileDialog(true, "Pulse Engine Prefab (*.pfb)\0*.pfb\0", "pfb", path, MAX_PATH, suggested, dir.c_str())) return;
    std::vector<int> sub;
    g.scene.collectSubtree(sel->id, sub);
    if (writeFile(path, g.scene.serializeSubset(sub))) {
        std::error_code ec;
        std::string rel = fs::relative(path, g.projectDir, ec).string();
        if (!ec) for (int id : sub) if (Entity* member = g.scene.byId(id)) {
            snprintf(member->prefabAsset, sizeof(member->prefabAsset), "%s", rel.c_str());
            member->prefabInstanceRoot = sel->id;
        }
        addLog(1, "Prefab saved: %s (%d objects)", path, (int)sub.size());
        scanBrowser();
    } else {
        addLog(2, "Could not write the prefab.");
    }
}

// ═══ project hub (launcher) ═══════════════════════════════════════════════
static std::string hubConfigPath() { return g.baseDir + "\\hub.cfg"; }

static void saveHub() {
    std::string out;
    for (const auto& p : g.hubProjects) out += p.path + "|" + p.lastLevel + "\n";
    writeFile(hubConfigPath(), out);
}

// add (or move to front) a project in the registry, refreshing its last level
static void hubAdd(const std::string& dir, const std::string& lastLevel) {
    std::string norm = dir;
    while (!norm.empty() && (norm.back() == '\\' || norm.back() == '/')) norm.pop_back();
    HubProject hp;
    hp.path = norm;
    hp.name = fs::path(norm).filename().string();
    if (hp.name.empty()) hp.name = norm;
    hp.lastLevel = lastLevel;
    for (int i = 0; i < (int)g.hubProjects.size(); i++) {
        if (_stricmp(g.hubProjects[i].path.c_str(), norm.c_str()) == 0) {
            if (lastLevel.empty()) hp.lastLevel = g.hubProjects[i].lastLevel;   // keep known
            g.hubProjects.erase(g.hubProjects.begin() + i);
            break;
        }
    }
    g.hubProjects.insert(g.hubProjects.begin(), hp);
    saveHub();
}

static void hubRemove(int i) {
    if (i < 0 || i >= (int)g.hubProjects.size()) return;
    g.hubProjects.erase(g.hubProjects.begin() + i);
    saveHub();
}

static void loadHub() {
    g.hubProjects.clear();
    std::string data;
    readFile(hubConfigPath(), data);
    size_t i = 0;
    while (i < data.size()) {
        size_t nl = data.find('\n', i);
        std::string line = data.substr(i, nl == std::string::npos ? std::string::npos : nl - i);
        i = (nl == std::string::npos) ? data.size() : nl + 1;
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
        if (line.empty()) continue;
        size_t bar = line.find('|');
        HubProject hp;
        hp.path = bar == std::string::npos ? line : line.substr(0, bar);
        hp.lastLevel = bar == std::string::npos ? "" : line.substr(bar + 1);
        hp.name = fs::path(hp.path).filename().string();
        if (hp.name.empty()) hp.name = hp.path;
        g.hubProjects.push_back(hp);
    }
    // migration: register the built-in sample folder so existing work stays reachable
    std::error_code ec;
    std::string sample = (fs::path(g.baseDir) / "progetto").string();
    if (fs::exists(sample, ec)) {
        bool known = false;
        for (const auto& p : g.hubProjects) if (_stricmp(p.path.c_str(), sample.c_str()) == 0) known = true;
        if (!known) {
            HubProject hp;
            hp.path = sample;
            hp.name = "progetto";
            g.hubProjects.push_back(hp);
        }
    }
}

static void ensureProjectDirs(const std::string& dir) {
    std::error_code ec;
    fs::create_directories(fs::path(dir) / "Levels", ec);
    fs::create_directories(fs::path(dir) / "Scripts", ec);
    fs::create_directories(fs::path(dir) / "prefab", ec);
}

static void loadGameplayProjectSettings() {
    g.gameInstanceAsset.clear();
    g.defaultGameModeAsset.clear();
    g.startupLevel.clear();
    g.language = "English";
    std::string data;
    if (!readFile((fs::path(g.projectDir) / "impulso.project").string(), data)) return;
    std::istringstream input(data);
    std::string line;
    auto value = [](const std::string& s) { return s == "-" ? std::string() : s; };
    while (std::getline(input, line)) {
        while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) line.pop_back();
        if (line.rfind("gameinstance ", 0) == 0) g.gameInstanceAsset = value(line.substr(13));
        else if (line.rfind("gamemode ", 0) == 0) g.defaultGameModeAsset = value(line.substr(9));
        else if (line.rfind("startlevel ", 0) == 0) g.startupLevel = value(line.substr(11));
        else if (line.rfind("language ", 0) == 0) { std::string v = value(line.substr(9)); if (!v.empty()) g.language = v; }
    }
}

static void saveGameplayProjectSettings() {
    std::ostringstream output;
    output << "Impulso project\n";
    output << "gameinstance " << (g.gameInstanceAsset.empty() ? "-" : g.gameInstanceAsset) << "\n";
    output << "gamemode " << (g.defaultGameModeAsset.empty() ? "-" : g.defaultGameModeAsset) << "\n";
    output << "startlevel " << (g.startupLevel.empty() ? "-" : g.startupLevel) << "\n";
    output << "language " << (g.language.empty() ? "-" : g.language) << "\n";
    writeFile((fs::path(g.projectDir) / "impulso.project").string(), output.str());
}

// scaffold a fresh project: folders, a marker file and a starter level
static void scaffoldProject(const std::string& dir) {
    ensureProjectDirs(dir);
    std::error_code ec;
    std::string marker = (fs::path(dir) / "impulso.project").string();
    if (!fs::exists(marker, ec)) writeFile(marker, "Impulso project\n");
    bool hasLevel = false;
    for (const auto& e : fs::directory_iterator(fs::path(dir) / "Levels", ec))
        if (e.path().extension() == ".imp") hasLevel = true;
    if (!hasLevel) {
        EditorScene tmp;
        sceneDefault(tmp);
        writeFile((fs::path(dir) / "Levels" / "Main.imp").string(), tmp.serialize());
    }
}

// leave the hub and load a project into the editor
static void openProjectAt(const std::string& dir, const std::string& lastLevelRel) {
    if (g.mode == Mode::Play) stopPlay();
    g.bpDocs.clear();
    g.curveDocs.clear();
    g.materialDocs.clear();
    g.widgetDocs.clear();
    g.activeDoc = 0;
    g.closeDocRequest = -1;
    g.closeCurveDocRequest = -1;
    g.buildScenesScanned = false;
    g.buildScenes.clear();
    g.buildSceneSelected = -1;
    g.drawers.clear();
    g.hasLastDrawerBrowser = false;
    g.lastDrawerBrowser = BrowserState{};
    std::string norm = dir;
    while (!norm.empty() && (norm.back() == '\\' || norm.back() == '/')) norm.pop_back();
    g.projectDir = norm;
    gBPProjectDir = g.projectDir;
    g.projectName = fs::path(norm).filename().string();
    if (g.projectName.empty()) g.projectName = "progetto";
    ensureProjectDirs(norm);
    loadGameplayProjectSettings();
    g.persistentGameInstanceVars.clear();
    g.curRel = "";
    g.projectPath[0] = 0;
    g.selectedId = 0;
    std::error_code ec;
    std::string levelAbs;
    // the project's configured startup scene wins over the last-opened level
    if (!g.startupLevel.empty() && fs::exists(norm + "\\" + g.startupLevel, ec)) levelAbs = norm + "\\" + g.startupLevel;
    else if (!lastLevelRel.empty() && fs::exists(norm + "\\" + lastLevelRel, ec)) levelAbs = norm + "\\" + lastLevelRel;
    else if (fs::exists(norm + "\\Levels\\Main.imp", ec)) levelAbs = norm + "\\Levels\\Main.imp";
    if (!levelAbs.empty()) openProjectFile(levelAbs);
    else { sceneDefault(g.scene); addLog(1, "New level (no .imp found)."); }
    scanBrowser();
    g.camera.target = { 0, 2, 0 };
    g.camera.distance = 15;
    g.inHub = false;
    hubAdd(norm, lastLevelRel);
    addLog(1, "Project opened: %s", g.projectName.c_str());
}

// native folder picker (also lets the user create a new folder in-dialog)
static bool pickFolder(char* out, int cap, const char* title) {
    BROWSEINFOA bi = {};
    bi.hwndOwner = g.hwnd;
    bi.lpszTitle = title;
    bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE | BIF_EDITBOX;
    LPITEMIDLIST pidl = SHBrowseForFolderA(&bi);
    if (!pidl) return false;
    char path[MAX_PATH] = "";
    bool ok = SHGetPathFromIDListA(pidl, path) != FALSE;
    CoTaskMemFree(pidl);
    if (ok) snprintf(out, cap, "%s", path);
    return ok && path[0];
}

static std::string cleanBuildName(std::string name) {
    if (name.empty()) name = "PulseGame";
    for (char& c : name) {
        unsigned char uc = (unsigned char)c;
        if (!(std::isalnum(uc) || c == '-' || c == '_')) c = '_';
    }
    return name;
}

static void scanBuildScenes(bool preserve = true) {
    std::map<std::string, bool> previous;
    if (preserve) for (const auto& e : g.buildScenes) previous[e.rel] = e.include;
    std::vector<std::string> found;
    std::error_code ec;
    for (fs::recursive_directory_iterator it(g.projectDir, fs::directory_options::skip_permission_denied, ec), end;
         it != end; it.increment(ec)) {
        if (ec) { ec.clear(); continue; }
        if (!it->is_regular_file(ec)) continue;
        std::string ext = it->path().extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return (char)tolower(c); });
        if (ext == ".imp") found.push_back(fs::relative(it->path(), g.projectDir, ec).string());
    }
    std::sort(found.begin(), found.end());
    std::vector<BuildSceneEntry> ordered;
    // Keep the user's existing order and append newly discovered scenes.
    if (preserve) {
        for (const auto& old : g.buildScenes)
            if (std::find(found.begin(), found.end(), old.rel) != found.end()) ordered.push_back(old);
    }
    for (const auto& rel : found) {
        bool exists = false;
        for (const auto& e : ordered) if (_stricmp(e.rel.c_str(), rel.c_str()) == 0) { exists = true; break; }
        if (!exists) ordered.push_back({ rel, previous.count(rel) ? previous[rel] : true });
    }
    g.buildScenes = std::move(ordered);
    g.buildScenesScanned = true;
    if (g.buildSceneSelected >= (int)g.buildScenes.size()) g.buildSceneSelected = (int)g.buildScenes.size() - 1;
}

static bool pathStartsWith(const fs::path& child, const fs::path& parent) {
    std::error_code ec;
    fs::path a = fs::weakly_canonical(child, ec);
    if (ec) { ec.clear(); a = fs::absolute(child, ec); }
    fs::path b = fs::weakly_canonical(parent, ec);
    if (ec) { ec.clear(); b = fs::absolute(parent, ec); }
    auto ai = a.begin(), bi = b.begin();
    for (; bi != b.end(); ++bi, ++ai)
        if (ai == a.end() || _stricmp(ai->string().c_str(), bi->string().c_str()) != 0) return false;
    return true;
}

static bool packageWindowsBuild() {
    if (g.mode == Mode::Play) {
        g.buildStatus = "Stop the simulation before creating a build.";
        return false;
    }
    if (!g.buildOutput[0]) {
        g.buildStatus = "Choose the destination folder first.";
        return false;
    }
    scanBuildScenes(true);
    std::vector<std::string> selected;
    for (const auto& e : g.buildScenes) if (e.include) selected.push_back(e.rel);
    if (selected.empty()) {
        g.buildStatus = "Select at least one scene.";
        return false;
    }

    // Persist unsaved changes before copying the project into the package.
    if (g.projectPath[0]) saveProject(false);

    std::string safeName = cleanBuildName(g.projectName);
    fs::path target = fs::path(g.buildOutput) / (safeName + "_Windows");
    if (pathStartsWith(target, g.projectDir)) {
        g.buildStatus = "The build must live outside the project folder.";
        return false;
    }
    std::error_code ec;
    if (fs::exists(target, ec)) {
        std::string question = "The folder already exists and will be replaced:\n" + target.string() + "\n\nContinue?";
        if (MessageBoxA(g.hwnd, question.c_str(), "Build Windows", MB_YESNO | MB_ICONWARNING) != IDYES) {
            g.buildStatus = "Build cancelled.";
            return false;
        }
        fs::remove_all(target, ec);
        if (ec) { g.buildStatus = "Could not replace the previous build."; return false; }
    }
    fs::create_directories(target / "progetto", ec);
    if (ec) { g.buildStatus = "Could not create the build folder."; return false; }

    char exePath[MAX_PATH] = "";
    GetModuleFileNameA(nullptr, exePath, MAX_PATH);
    fs::copy_file(exePath, target / (safeName + ".exe"), fs::copy_options::overwrite_existing, ec);
    if (ec) { g.buildStatus = "Could not copy the Windows executable."; return false; }

    auto isSelectedScene = [&](const std::string& rel) {
        for (const auto& s : selected) if (_stricmp(s.c_str(), rel.c_str()) == 0) return true;
        return false;
    };
    for (fs::recursive_directory_iterator it(g.projectDir, fs::directory_options::skip_permission_denied, ec), end;
         it != end; it.increment(ec)) {
        if (ec) { ec.clear(); continue; }
        if (!it->is_regular_file(ec)) continue;
        std::string rel = fs::relative(it->path(), g.projectDir, ec).string();
        if (ec) { ec.clear(); continue; }
        std::string ext = it->path().extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return (char)tolower(c); });
        if (ext == ".imp" && !isSelectedScene(rel)) continue;
        fs::path dst = target / "progetto" / rel;
        fs::create_directories(dst.parent_path(), ec);
        if (ec) break;
        fs::copy_file(it->path(), dst, fs::copy_options::overwrite_existing, ec);
        if (ec) break;
    }
    if (ec) { g.buildStatus = "Error while copying the project assets."; return false; }

    std::string manifest = "IMPULSO_BUILD 1\nproject " + g.projectName + "\n";
    for (const auto& rel : selected) manifest += "scene " + rel + "\n";
    if (!writeFile((target / "impulso_build.cfg").string(), manifest)) {
        g.buildStatus = "Could not write the build manifest.";
        return false;
    }
    g.buildStatus = "Build complete: " + target.string();
    addLog(1, "%s", g.buildStatus.c_str());
    MessageBoxA(g.hwnd, g.buildStatus.c_str(), "Build Windows", MB_OK | MB_ICONINFORMATION);
    return true;
}

static bool loadStandaloneManifest() {
    std::string data;
    if (!readFile(g.baseDir + "\\impulso_build.cfg", data) || data.rfind("IMPULSO_BUILD 1", 0) != 0) return false;
    g.standaloneScenes.clear();
    size_t pos = 0;
    while (pos < data.size()) {
        size_t nl = data.find('\n', pos);
        std::string line = data.substr(pos, nl == std::string::npos ? std::string::npos : nl - pos);
        pos = nl == std::string::npos ? data.size() : nl + 1;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.rfind("project ", 0) == 0) g.projectName = line.substr(8);
        else if (line.rfind("scene ", 0) == 0 && line.size() > 6) g.standaloneScenes.push_back(line.substr(6));
    }
    g.standaloneMode = !g.standaloneScenes.empty();
    return g.standaloneMode;
}

static void hubCreateProject() {
    char dir[MAX_PATH];
    if (!pickFolder(dir, sizeof(dir), "Choose (or create) the folder for the new project")) return;
    scaffoldProject(dir);
    openProjectAt(dir, "Levels\\Main.imp");
}

static void hubConnectProject() {
    char dir[MAX_PATH];
    if (!pickFolder(dir, sizeof(dir), "Choose the folder of an existing project")) return;
    ensureProjectDirs(dir);
    openProjectAt(dir, "");
}

// save the current level path for the project, then show the hub again
static void returnToHub() {
    if (g.mode == Mode::Play) stopPlay();
    std::string rel;
    if (g.projectPath[0]) {
        std::string abs = g.projectPath;
        if (abs.rfind(g.projectDir, 0) == 0 && abs.size() > g.projectDir.size() + 1)
            rel = abs.substr(g.projectDir.size() + 1);
    }
    hubAdd(g.projectDir, rel);
    g.bpDocs.clear();
    g.curveDocs.clear();
    g.materialDocs.clear();
    g.widgetDocs.clear();
    g.activeDoc = 0;
    g.inHub = true;
    g.hubScroll = 0;
}

// ═══ clipboard (Ctrl+C / Ctrl+V) ═══
static Vec3 dropPos();
static std::string relJoin(const std::string& a, const std::string& b);
static std::string relAbs(const std::string& rel);

static void copySelection() {
    Entity* sel = g.scene.byId(g.selectedId);
    if (!sel) { addLog(2, "Niente da copiare: seleziona un oggetto."); return; }
    std::vector<int> sub;
    g.scene.collectSubtree(sel->id, sub);
    g.clipboard = g.scene.serializeSubset(sub);
    addLog(1, "Copied: %s (%d objects). Ctrl+V to paste.", sel->name, (int)sub.size());
}

static void pasteClipboard() {
    if (g.clipboard.empty()) { addLog(2, "Clipboard empty: copy with Ctrl+C first."); return; }
    std::vector<int> ids = g.scene.instantiateFrom(g.clipboard, dropPos(), true);
    if (ids.empty()) { addLog(2, "Paste failed: invalid clipboard."); return; }
    for (int id : ids) {
        Entity* e = g.scene.byId(id);
        if (e && (e->parentId == 0 || !g.scene.byId(e->parentId))) { g.selectedId = id; break; }
    }
    addLog(1, "Pasted (%d objects).", (int)ids.size());
}

static void duplicateSceneEntity(int entityId) {
    Entity* source = g.scene.byId(entityId);
    if (!source) { addLog(2, "Select an object first."); return; }
    std::vector<int> ids = g.scene.duplicateSubtree(entityId);
    int root = 0;
    for (int id : ids) {
        Entity* copy = g.scene.byId(id);
        if (copy && std::find(ids.begin(), ids.end(), copy->parentId) == ids.end()) { root = id; break; }
    }
    if (!root && !ids.empty()) root = ids.front();
    g.selectedId = root;
    g.selectedIds.clear();
    if (root) g.selectedIds.insert(root);
    g.outlinerSelectionAnchor = root;
    addLog(0, "Duplicated (%d objects).", (int)ids.size());
}

static void addEmptyRelativeToEntity(int entityId,bool asParent){
    Entity* selected=g.scene.byId(entityId);
    if(!selected||!selected->body)return;
    const Vec3 worldPosition=selected->body->position;
    const Quat worldRotation=selected->body->quat;
    const int previousParent=selected->parentId;
    const std::string selectedName=selected->name;
    char name[48];
    snprintf(name,sizeof(name),asParent?"Empty Parent %d":"Empty Child %d",g.scene.nextEntityId);
    Entity& created=g.scene.spawnEmpty(name,worldPosition);
    const int createdId=created.id;
    created.body->quat=worldRotation;
    created.body->updateInertiaWorld();
    created.body->updateAABB();
    if(asParent){
        if(previousParent)g.scene.setParent(createdId,previousParent);
        g.scene.setParent(entityId,createdId);
    }else{
        g.scene.setParent(createdId,entityId);
    }
    // Only hierarchy links are changed. In particular, prefabAsset and
    // prefabInstanceRoot remain on the original prefab subtree.
    g.selectedId=createdId;g.selectedIds.clear();g.selectedIds.insert(createdId);
    g.outlinerSelectionAnchor=createdId;
    addLog(1,asParent?"Empty parent added above '%s'.":"Empty child added to '%s'.",selectedName.c_str());
}

static Vec3 dropPos() {
    Vec3 t = g.camera.target;
    return { t.x + (rand() % 100 - 50) / 100.0f, t.y + 2.0f, t.z + (rand() % 100 - 50) / 100.0f };
}

static int markPrefabInstance(const std::vector<int>& ids, const std::string& rel) {
    int root = 0;
    for (int id : ids) {
        Entity* e = g.scene.byId(id);
        if (e && (e->parentId == 0 || std::find(ids.begin(), ids.end(), e->parentId) == ids.end())) { root=id; break; }
    }
    if (!root && !ids.empty()) root=ids.front();
    for (int id : ids) if (Entity* e=g.scene.byId(id)) {
        snprintf(e->prefabAsset,sizeof(e->prefabAsset),"%s",rel.c_str());
        e->prefabInstanceRoot=root;
    }
    return root;
}

static void instantiatePrefabAt(const std::string& name, const Vec3& position) {
    std::string rel=relJoin(g.curRel,name);
    std::string data;
    if (!readFile(relAbs(rel), data)) { addLog(2, "Could not read %s", name.c_str()); return; }
    std::vector<int> ids = g.scene.instantiateFrom(data, position, true);
    if (ids.empty()) { addLog(2, "Invalid prefab: %s", name.c_str()); return; }
    g.selectedId=markPrefabInstance(ids,rel);
    g.selectedIds.clear();if(g.selectedId)g.selectedIds.insert(g.selectedId);
    addLog(1, "Instantiated prefab %s (%d objects)", name.c_str(), (int)ids.size());
}

static void instantiatePrefab(const std::string& name) { instantiatePrefabAt(name,dropPos()); }

static Vec3 prefabDropPosition() {
    Vec3 origin,dir;mouseRay(origin,dir);
    if(fabsf(dir.y)>.00001f){float t=-origin.y/dir.y;if(t>0)return origin+dir*t;}
    return g.camera.target;
}

static void propagatePrefabInstances(const std::string& rel,const std::string& data) {
    // Per-instance Blueprint variable overrides (e.g. Object references dragged into
    // the Details) are captured before the instance is rebuilt and restored onto the
    // fresh one, so an Override Prefab does not wipe each instance's own bindings.
    struct InstancePose { int root,parent,nextSibling;Vec3 position,scale;Quat rotation;
        std::string bpPath; std::map<std::string,Vec3> ov; std::map<std::string,float> aov;
        std::vector<BlueprintComponentDef> addBp; };
    std::vector<InstancePose> instances;
    for(size_t index=0;index<g.scene.entities.size();index++){
        const Entity& e=g.scene.entities[index];
        if(e.prefabInstanceRoot!=e.id||_stricmp(e.prefabAsset,rel.c_str())!=0)continue;
        int nextSibling=0;
        for(size_t next=index+1;next<g.scene.entities.size();next++)
            if(g.scene.entities[next].parentId==e.parentId){nextSibling=g.scene.entities[next].id;break;}
        instances.push_back({e.id,e.parentId,nextSibling,e.body->position,e.scale,e.body->quat,
                             e.graphPath,e.varOverrides,e.varAlphaOverrides,e.additionalBlueprints});
    }
    for(const InstancePose& old:instances){
        if(g.scene.byId(old.root))g.scene.removeEntity(old.root);
        std::vector<int> ids=g.scene.instantiateFrom(data,old.position,true);if(ids.empty())continue;
        int root=markPrefabInstance(ids,rel);Entity* fresh=g.scene.byId(root);if(!fresh)continue;
        Vec3 pivot=fresh->body->position,fromScale=fresh->scale;Quat fromRotation=fresh->body->quat;
        g.scene.scaleDescendants(root,pivot,fromRotation,fromScale,old.scale);fresh->scale=old.scale;g.scene.syncBodyShape(*fresh);
        g.scene.rotateDescendants(root,pivot,fromRotation,old.rotation);fresh->body->quat=old.rotation;fresh->body->updateAABB();
        Vec3 delta=old.position-fresh->body->position;g.scene.moveDescendants(root,delta);fresh->body->position=old.position;fresh->body->updateAABB();
        if(old.parent&&g.scene.byId(old.parent))g.scene.setParent(root,old.parent);
        // restore this instance's own Blueprint variable overrides (match by graph path)
        if(Entity* fr=g.scene.byId(root)){
            if(fr->graphPath==old.bpPath){fr->varOverrides=old.ov;fr->varAlphaOverrides=old.aov;}
            for(const BlueprintComponentDef& cap:old.addBp)
                for(BlueprintComponentDef& fb:fr->additionalBlueprints)
                    if(fb.graphPath==cap.graphPath){fb.varOverrides=cap.varOverrides;fb.varAlphaOverrides=cap.varAlphaOverrides;break;}
        }
        // instantiateFrom appends the refreshed root, which used to move the
        // instance to the bottom of its Outliner sibling list. Reinsert only
        // the root before its old next sibling; child order is already carried
        // by the prefab serialization.
        if(old.nextSibling){
            auto moved=std::find_if(g.scene.entities.begin(),g.scene.entities.end(),[&](const Entity& e){return e.id==root;});
            auto before=std::find_if(g.scene.entities.begin(),g.scene.entities.end(),[&](const Entity& e){return e.id==old.nextSibling;});
            if(moved!=g.scene.entities.end()&&before!=g.scene.entities.end()){
                Entity refreshed=std::move(*moved);
                g.scene.entities.erase(moved);
                before=std::find_if(g.scene.entities.begin(),g.scene.entities.end(),[&](const Entity& e){return e.id==old.nextSibling;});
                g.scene.entities.insert(before,std::move(refreshed));
            }
        }
    }
}

static Entity* prefabInstanceRoot(Entity& selected) {
    if (selected.prefabInstanceRoot) {
        Entity* root = g.scene.byId(selected.prefabInstanceRoot);
        if (root && root->prefabAsset[0]) return root;
    }
    Entity* current = &selected;
    for (int guard = 0; current && guard < 128; guard++) {
        if (current->prefabAsset[0] && current->prefabInstanceRoot == current->id) return current;
        current = current->parentId ? g.scene.byId(current->parentId) : nullptr;
    }
    return nullptr;
}

static bool overridePrefabFromEntity(Entity& selected) {
    Entity* root = prefabInstanceRoot(selected);
    if (!root || !root->prefabAsset[0]) return false;
    const std::string relative = root->prefabAsset;
    const Vec3 previousPosition = root->body ? root->body->position : Vec3{};
    std::vector<int> ids; g.scene.collectSubtree(root->id, ids);
    std::string data = g.scene.serializeSubset(ids);
    if (!writeFile(relAbs(relative), data)) {
        addLog(2, "Could not update prefab %s.", relative.c_str());
        return false;
    }
    propagatePrefabInstances(relative, data);
    g.selectedId = 0;
    float nearest = (std::numeric_limits<float>::max)();
    for (Entity& candidate : g.scene.entities) if (candidate.prefabInstanceRoot==candidate.id &&
        _stricmp(candidate.prefabAsset,relative.c_str())==0 && candidate.body) {
        float distance=candidate.body->position.distanceTo(previousPosition);
        if(distance<nearest){nearest=distance;g.selectedId=candidate.id;}
    }
    g.selectedIds.clear();
    if(g.selectedId)g.selectedIds.insert(g.selectedId);
    scanBrowser();
    addLog(1, "Prefab override applied to every instance: %s", relative.c_str());
    return true;
}

static bool savePrefabEdit() {
    if(!g.prefabEditMode)return false;
    int rootId=0;
    for(const Entity& entity:g.scene.entities)
        if(entity.parentId==0||!g.scene.byId(entity.parentId)){rootId=entity.id;break;}
    if(!rootId){addLog(2,"Prefab has no root: cannot save.");return false;}
    // Objects created while the isolated prefab workspace is open start as
    // ordinary scene entities. Adopt every object before serialization so new
    // children immediately receive the same Outliner tint and prefab identity.
    for(Entity& entity:g.scene.entities){
        snprintf(entity.prefabAsset,sizeof(entity.prefabAsset),"%s",g.prefabEditRel.c_str());
        entity.prefabInstanceRoot=rootId;
    }
    std::vector<int> ids;for(const Entity&e:g.scene.entities)ids.push_back(e.id);
    std::string data=g.scene.serializeSubset(ids);
    if(!writeFile(relAbs(g.prefabEditRel),data)){addLog(2,"Could not save prefab %s.",g.prefabEditRel.c_str());return false;}
    addLog(1,"Prefab salvato: %s",g.prefabEditRel.c_str());return true;
}

static Entity* prefabEditRoot() {
    if(!g.prefabEditMode)return nullptr;
    for(Entity& e:g.scene.entities)if(e.parentId==0||!g.scene.byId(e.parentId))return &e;
    return nullptr;
}

static void normalizePrefabEditSpace() {
    Entity* root=prefabEditRoot();if(!root||!root->body)return;
    Vec3 pivot=root->body->position;Quat rotation=root->body->quat;
    Quat identity{};
    g.scene.rotateDescendants(root->id,pivot,rotation,identity);
    root->body->quat=identity;root->body->updateInertiaWorld();root->body->updateAABB();
    Vec3 delta=Vec3{}-root->body->position;
    g.scene.moveDescendants(root->id,delta);
    root->body->position={};root->body->velocity={};root->body->angularVelocity={};root->body->updateAABB();
}

static void closePrefabEdit(bool saveChanges) {
    if(!g.prefabEditMode)return;
    std::string rel=g.prefabEditRel,data;
    if(saveChanges&&!savePrefabEdit())return;
    readFile(relAbs(rel),data);
    std::string backup=g.prefabEditSceneBackup;int previous=g.prefabEditPreviousSelection;
    g.prefabEditMode=false;g.prefabEditRel.clear();g.prefabEditSceneBackup.clear();
    if(!g.scene.deserialize(backup)){addLog(2,"Could not restore the level after Prefab Mode.");return;}
    if(saveChanges&&!data.empty())propagatePrefabInstances(rel,data);
    g.selectedId=g.scene.byId(previous)?previous:0;g.selectedIds.clear();if(g.selectedId)g.selectedIds.insert(g.selectedId);
    addLog(1,saveChanges?"Prefab applied to every instance in the scene.":"Prefab Mode closed without saving.");
}

static void openPrefabEditor(const std::string& rel) {
    if(g.prefabEditMode)closePrefabEdit(false);
    std::string data;if(!readFile(relAbs(rel),data)){addLog(2,"Prefab not readable: %s",rel.c_str());return;}
    std::string backup=g.scene.serialize();int previous=g.selectedId;
    if(!g.scene.deserialize(data)){addLog(2,"Invalid prefab: %s",rel.c_str());return;}
    g.prefabEditMode=true;g.prefabEditRel=rel;g.prefabEditSceneBackup=backup;g.prefabEditPreviousSelection=previous;
    normalizePrefabEditSpace();
    g.selectedId=0;for(const Entity&e:g.scene.entities)if(e.parentId==0){g.selectedId=e.id;break;}
    g.selectedIds.clear();if(g.selectedId)g.selectedIds.insert(g.selectedId);
    g.camera.target={};g.camera.distance=8;
    addLog(1,"Prefab Mode: %s",rel.c_str());
}

// ═══ prefab spawns ═══
static void spawnPrefab(int what) {
    EditorScene& s = g.scene;
    char nm[48];
    Entity* made = nullptr;
    switch (what) {
    case 0:
        snprintf(nm, sizeof(nm), "Cubo %d", s.nextEntityId);
        made = &s.spawnBox(nm, dropPos(), { 1, 1, 1 }, { 0.85f, 0.55f, 0.25f }, BodyType::Dynamic, 1);
        break;
    case 1:
        snprintf(nm, sizeof(nm), "Sfera %d", s.nextEntityId);
        made = &s.spawnSphere(nm, dropPos(), 1, { 0.35f, 0.65f, 0.95f });
        break;
    case 2: {
        snprintf(nm, sizeof(nm), "Cilindro %d", s.nextEntityId);
        Entity& e = s.spawnBox(nm, dropPos(), { 1, 1.2f, 1 }, { 0.6f, 0.85f, 0.45f }, BodyType::Dynamic, 1);
        e.mesh = MESH_CYLINDER;
        s.syncBodyShape(e);
        made = &e;
        break;
    }
    case 3:
        snprintf(nm, sizeof(nm), "Luce %d", s.nextEntityId);
        made = &s.spawnLight(nm, dropPos() + Vec3{ 0, 1, 0 });
        break;
    case 8: {
        snprintf(nm, sizeof(nm), "Capsula %d", s.nextEntityId);
        Entity& e = s.spawnBox(nm, dropPos() + Vec3{ 0, 0.5f, 0 }, { 0.8f, 0.8f, 0.8f }, { 0.75f, 0.6f, 0.9f }, BodyType::Dynamic, 1);
        e.mesh = MESH_CAPSULE;
        s.syncBodyShape(e);
        made = &e;
        break;
    }
    case 9: {
        snprintf(nm, sizeof(nm), "Trigger %d", s.nextEntityId);
        Entity& e = s.spawnBox(nm, dropPos() + Vec3{ 0, 1, 0 }, { 2, 2, 2 }, { 0.3f, 0.9f, 0.5f }, BodyType::Static);
        e.hasMesh = false;
        e.hasPhysics = false;
        e.hasTrigger = true;
        e.collision = 1;     // backward compatibility for older scene readers
        s.syncBodyShape(e);
        made = &e;
        break;
    }
    case 10: {
        // scene camera: an object that carries only the Camera component
        snprintf(nm, sizeof(nm), "Camera %d", s.nextEntityId);
        Entity& e = s.spawnBox(nm, dropPos() + Vec3{ 0, 1.5f, 0 }, { 0.4f, 0.4f, 0.4f },
                               { 0.4f, 0.85f, 0.95f }, BodyType::Static);
        e.hasMesh = false;
        e.hasPhysics = false;
        e.body->enabled = false;
        e.isCamera = true;
        s.syncBodyShape(e);
        made = &e;
        break;
    }
    case 11: {
        snprintf(nm, sizeof(nm), "Cono %d", s.nextEntityId);
        Entity& e = s.spawnBox(nm, dropPos(), { 1, 1.2f, 1 }, { 0.9f, 0.58f, 0.35f }, BodyType::Dynamic, 1);
        e.mesh = MESH_CONE;
        s.syncBodyShape(e);
        made = &e;
        break;
    }
    case 12:
        snprintf(nm, sizeof(nm), "Empty Object %d", s.nextEntityId);
        made = &s.spawnEmpty(nm, dropPos());
        break;
    case 13: {
        snprintf(nm, sizeof(nm), "Audio Source %d", s.nextEntityId);
        Entity& e = s.spawnEmpty(nm, dropPos());
        e.hasAudio = true;
        made = &e;
        break;
    }
    case 14: {
        snprintf(nm, sizeof(nm), "Reverb Zone %d", s.nextEntityId);
        Entity& e = s.spawnEmpty(nm, dropPos());
        e.hasReverb = true;
        made = &e;
        break;
    }
    case 15: {
        snprintf(nm, sizeof(nm), "AI Agent %d", s.nextEntityId);
        Entity& e = s.spawnEmpty(nm, dropPos());
        e.hasAIAgent = true;
        e.aiDestination = e.body->position;
        made = &e;
        break;
    }
    case 16: {
        snprintf(nm, sizeof(nm), "Physics Constraint %d", s.nextEntityId);
        Entity& e = s.spawnEmpty(nm, dropPos());
        e.hasConstraint = true;
        made = &e;
        break;
    }
    case 4:
        made = &s.spawnBox("Wall", dropPos(), { 6, 3, 0.5f }, { 0.5f, 0.52f, 0.58f }, BodyType::Static);
        made->staticFlags = STATIC_MOVEMENT | STATIC_NAVIGATION;
        break;
    case 5: {
        bool has = false;
        for (auto& e : s.entities) if (strcmp(e.name, "Floor") == 0) has = true;
        if (has) { addLog(2, "The scene already has a floor."); return; }
        Entity& e = s.spawnBox("Floor", { 0, -0.5f, 0 }, { 26, 1, 26 }, { 0.42f, 0.45f, 0.5f }, BodyType::Static, 0, 0.25f, 0.7f);
        e.checker = 2; e.shininess = 24; e.specular = 0.12f;
        e.staticFlags = STATIC_MOVEMENT | STATIC_NAVIGATION;
        made = &e;
        break;
    }
    case 6: {
        Vec3 t = g.camera.target;
        for (int i = 0; i < 5; i++) {
            snprintf(nm, sizeof(nm), "Stack %d-%d", s.nextEntityId, i + 1);
            made = &s.spawnBox(nm, { t.x, 0.5f + i * 1.01f, t.z }, { 1, 1, 1 },
                               { 0.4f + i * 0.12f, 0.55f, 0.85f - i * 0.12f }, BodyType::Dynamic, 1, 0.1f, 0.6f);
        }
        break;
    }
    case 7: {
        Vec3 p = dropPos() + Vec3{ 0, 3, 0 };
        snprintf(nm, sizeof(nm), "Perno %d", s.nextEntityId);
        int anchorId = s.spawnBox(nm, p, { 0.24f, 0.24f, 0.24f }, { 0.3f, 0.32f, 0.38f }, BodyType::Static).id;
        snprintf(nm, sizeof(nm), "Pendolo %d", s.nextEntityId);
        Entity& ball = s.spawnSphere(nm, p + Vec3{ 2.2f, -2.2f, 0 }, 1, { 0.75f, 0.78f, 0.85f }, 2, 0.9f, 0.05f);
        ball.body->canSleep = false;
        int ballId = ball.id;
        s.addJoint(anchorId, ballId, -1, false);
        s.setParent(ballId, anchorId);
        made = g.scene.byId(ballId);
        break;
    }
    }
    if (made) {
        g.selectedId = made->id;
        addLog(0, "Added: %s", made->name);
    }
}

// ═══ dock window contents ═══
// drop target di una variabile esposta (Object/Transform): l'Outliner ci lascia cadere un oggetto
struct VarDropTarget {
    float x, y, w, h;
    int entId;
    int blueprintComponent = 0;
    char var[32];
    PinKind type;
    char refClass[96];
    int inspectorEvent = -1;
    int inspectorListener = -1;
    int constraintSlot = 0;   // 1 = Physics Constraint Object 1, 2 = Object 2
};
static std::vector<VarDropTarget> g_varDrops;

static bool entityMatchesRefClass(const Entity& e, const char* refClass) {
    if (!refClass || !refClass[0]) return true;
    if (strcmp(refClass, "component:Camera") == 0) return e.isCamera;
    if (strcmp(refClass, "component:Light") == 0) return e.isLight;
    if (strcmp(refClass, "component:Mesh") == 0) return e.hasMesh;
    if (strcmp(refClass, "component:Physics") == 0) return e.hasPhysics;
    if (strcmp(refClass, "component:AudioSource") == 0) return e.hasAudio;
    if (strcmp(refClass, "component:ReverbZone") == 0) return e.hasReverb;
    if (strcmp(refClass, "component:AIAgent") == 0) return e.hasAIAgent;
    if (strcmp(refClass, "component:Animator") == 0) return e.hasAnimator;
    // Asset Object references are selected from the Blueprint variable Details,
    // never by dropping a scene entity from the Outliner.
    if (strcmp(refClass, "asset:AnimatorController") == 0) return false;
    const char* bp = "blueprint:";
    size_t n = strlen(bp);
    if (strncmp(refClass, bp, n) == 0) {
        for (int componentIndex = 0; componentIndex < entityBlueprintCount(e); componentIndex++)
            if (appBlueprintPathIsA(entityBlueprintPath(e, componentIndex), refClass + n)) return true;
        return false;
    }
    return false;
}

static std::string refClassLabel(const char* refClass) {
    if (!refClass || !refClass[0]) return "GameObject";
    if (strcmp(refClass, "component:Camera") == 0) return "Camera";
    if (strcmp(refClass, "component:Light") == 0) return "Light";
    if (strcmp(refClass, "component:Mesh") == 0) return "Mesh Renderer";
    if (strcmp(refClass, "component:Physics") == 0) return "Rigid Body";
    if (strcmp(refClass, "component:AudioSource") == 0) return "Audio Source";
    if (strcmp(refClass, "component:ReverbZone") == 0) return "Audio Reverb Zone";
    if (strcmp(refClass, "component:AIAgent") == 0) return "AI Agent";
    if (strcmp(refClass, "component:Animator") == 0) return "Animator";
    if (strcmp(refClass, "asset:AnimatorController") == 0) return "Animator Controller";
    const char* bp = "blueprint:";
    size_t n = strlen(bp);
    if (strncmp(refClass, bp, n) == 0) return std::string("Blueprint ") + fs::path(refClass + n).stem().string();
    return refClass;
}

enum SceneComponentAdd {
    ADD_MESH, ADD_RIGID_BODY, ADD_TRIGGER, ADD_LIGHT, ADD_CAMERA,
    ADD_AUDIO, ADD_REVERB, ADD_AI_AGENT, ADD_NAV_OCCLUDER,
    ADD_ANIMATOR, ADD_INSPECTOR_EVENTS, ADD_SIMPLE_SCRIPT, ADD_JOINT, ADD_CONSTRAINT
};

static bool addSceneComponent(Entity& e, int component, const std::string& blueprint = {}) {
    if (!blueprint.empty()) {
        bool added = addBlueprintComponent(e, blueprint);
        addLog(added ? 1 : 2, added ? "Blueprint component %s added to %s."
                                    : "Blueprint %s is single-instance and is already on %s.",
               fs::path(blueprint).stem().string().c_str(), e.name);
        return added;
    }
    switch (component) {
    case ADD_MESH: if (e.hasMesh) return false; e.hasMesh=true; g.scene.syncBodyShape(e); break;
    case ADD_RIGID_BODY: if (e.hasPhysics) return false; e.hasPhysics=true; g.scene.syncBodyShape(e); break;
    case ADD_TRIGGER: if (e.hasTrigger) return false; e.hasTrigger=true;e.collision=1;g.scene.syncBodyShape(e); break;
    case ADD_LIGHT: if (e.isLight) return false; e.isLight=true; break;
    case ADD_CAMERA: if (e.isCamera) return false; e.isCamera=true; break;
    case ADD_AUDIO: if (e.hasAudio) return false; e.hasAudio=true; break;
    case ADD_REVERB: if (e.hasReverb) return false; e.hasReverb=true; break;
    case ADD_AI_AGENT: if (e.hasAIAgent) return false; e.hasAIAgent=true;e.aiDestination=e.body->position; break;
    case ADD_NAV_OCCLUDER: if (e.hasNavigationOccluder) return false; e.hasNavigationOccluder=true;invalidateNavigation(); break;
    case ADD_ANIMATOR: if (e.hasAnimator) return false; e.hasAnimator=true;e.animatorController[0]=0;e.animatorPlayOnAwake=true;e.animatorSpeed=1; break;
    case ADD_INSPECTOR_EVENTS:
        if (e.hasInspectorEvents) return false;
        e.hasInspectorEvents=true; { InspectorEventDef event; event.name="Event1"; e.inspectorEvents.push_back(std::move(event)); }
        break;
    case ADD_SIMPLE_SCRIPT: if (e.behavior != BH_NONE) return false; e.behavior=BH_JUMP_SPACE; break;
    case ADD_CONSTRAINT: if (e.hasConstraint) return false; e.hasConstraint=true; break;
    case ADD_JOINT: {
        Entity* nearest = nullptr; float best = (std::numeric_limits<float>::max)();
        for (Entity& candidate : g.scene.entities) if (candidate.id != e.id && candidate.body) {
            float distance = candidate.body->position.distanceTo(e.body->position);
            if (distance < best) { best=distance;nearest=&candidate; }
        }
        if (!nearest) return false;
        g.scene.addJoint(e.id, nearest->id, -1, false);
        addLog(1, "Joint added between %s and %s.", e.name, nearest->name);
        return true;
    }
    default: return false;
    }
    addLog(1, "Component added to %s.", e.name);
    return true;
}

static std::vector<std::string> blueprintComponentAssets() {
    std::vector<std::string> result;
    std::error_code ec;
    for (fs::recursive_directory_iterator it(g.projectDir, fs::directory_options::skip_permission_denied, ec), end;
         !ec && it != end; it.increment(ec)) {
        if (!it->is_regular_file(ec) || _stricmp(it->path().extension().string().c_str(), ".bp") != 0) continue;
        std::string relative = fs::relative(it->path(), g.projectDir, ec).string();
        if (!ec) result.push_back(relative); else ec.clear();
    }
    std::sort(result.begin(), result.end());
    return result;
}

static void collectVisibleOutliner(int id, std::vector<int>& out) {
    Entity* e = g.scene.byId(id); if (!e) return;
    out.push_back(id);
    if (g.collapsed.count(id)) return;
    for (const Entity& child : g.scene.entities) if (child.parentId == id) collectVisibleOutliner(child.id, out);
}

static void drawOutlinerNode(UI& ui, int id, int depth) {
    Entity* e = g.scene.byId(id);
    if (!e) return;
    bool hasChildren = false;
    for (const auto& c : g.scene.entities) if (c.parentId == id) { hasChildren = true; break; }
    bool expanded = !g.collapsed.count(id);

    // per-type icon (replaces the old [#]/(o)/(L) text prefix)
    const char* icon = e->isLight ? "ent_light"
                     : e->isCamera ? "ent_camera"
                     : e->meshAsset[0] ? "ent_mesh"
                     : e->mesh == MESH_SPHERE ? "ent_sphere"
                     : e->mesh == MESH_CYLINDER ? "ent_cylinder"
                     : e->mesh == MESH_CONE ? "ent_cone"
                     : e->mesh == MESH_CAPSULE ? "ent_capsule"
                     : "ent_cube";
    char text[110];
    snprintf(text, sizeof(text), "%s%s%s", e->name,
             e->body->type == BodyType::Static ? " *" : "",
             e->behavior != BH_NONE ? " +s" : "");
    char idbuf[32];
    snprintf(idbuf, sizeof(idbuf), "ent%d", id);

    bool dropHi = g.treeDragging && g.treeDropId == id && g.treeDragId != id;
    bool selected = g.selectedIds.count(id) != 0 || (g.selectedIds.empty() && g.selectedId == id);
    int fl = ui.treeItem(idbuf, text, depth, hasChildren, expanded, selected, dropHi, e->prefabAsset[0] != 0, -1, nullptr, icon);
    if (fl & UI::TREE_TOGGLED) {
        if (expanded) g.collapsed.insert(id);
        else g.collapsed.erase(id);
    } else if (fl & UI::TREE_CLICKED) {
        const UIInput& input = ui.input();
        if (input.keyShift && g.outlinerSelectionAnchor) {
            auto a = std::find(g.outlinerVisibleOrder.begin(), g.outlinerVisibleOrder.end(), g.outlinerSelectionAnchor);
            auto b = std::find(g.outlinerVisibleOrder.begin(), g.outlinerVisibleOrder.end(), id);
            if (a != g.outlinerVisibleOrder.end() && b != g.outlinerVisibleOrder.end()) {
                if (!input.keyCtrl) g.selectedIds.clear();
                int ia=(int)(a-g.outlinerVisibleOrder.begin()), ib=(int)(b-g.outlinerVisibleOrder.begin());
                if (ia>ib) std::swap(ia,ib);
                for(int i=ia;i<=ib;i++) g.selectedIds.insert(g.outlinerVisibleOrder[i]);
            }
            g.selectedId=id;
        } else if (input.keyCtrl) {
            if (g.selectedIds.count(id)) g.selectedIds.erase(id); else g.selectedIds.insert(id);
            g.selectedId = g.selectedIds.count(id) ? id : (g.selectedIds.empty() ? 0 : *g.selectedIds.rbegin());
            g.outlinerSelectionAnchor=id;
        } else {
            g.selectedIds.clear(); g.selectedIds.insert(id); g.selectedId=id; g.outlinerSelectionAnchor=id;
        }
    }
    if (fl & UI::TREE_PRESSED) {
        g.treeDragId = id;
        g.treePressX = ui.input().mouseX;
        g.treePressY = ui.input().mouseY;
        g.treeDragging = false;
    }
    if (fl & UI::TREE_RCLICKED) {
        g.selectedIds.clear(); g.selectedIds.insert(id); g.selectedId=id; g.outlinerSelectionAnchor=id;
        g.outlinerContextEntity=id;
        g.outlinerContextX=ui.input().mouseX;
        g.outlinerContextY=ui.input().mouseY;
        g.outlinerComponentSubmenu=false;
        g.outlinerComponentScroll=0;
    }
    if ((fl & UI::TREE_HOVERED) && g.treeDragging) g.treeDropId = id;

    if (expanded) {
        for (const auto& c : g.scene.entities) {
            if (c.parentId == id) drawOutlinerNode(ui, c.id, depth + 1);
        }
    }
}

static void drawOutlinerContent(UI& ui) {
    if(g.prefabEditMode){
        if(ui.buttonColored("<  Back to level",{.12f,.28f,.48f},{.94f,.97f,1.0f})){
            closePrefabEdit(true);
            return;
        }
        ui.label("PREFAB  "+fs::path(g.prefabEditRel).stem().string(),{.48f,.74f,1.0f});
        ui.separator();
    }
    UIInput rawIn = ui.input();
    bool outlinerWasBlocked = ui.interactionBlocked();
    if (g.outlinerContextEntity) ui.setInteractionBlocked(true);
    const UIInput& in = ui.input();
    if (!g.selectedId) g.selectedIds.clear();
    else if (!g.selectedIds.count(g.selectedId)) { g.selectedIds.clear(); g.selectedIds.insert(g.selectedId); }
    g.outlinerVisibleOrder.clear();
    for (const Entity& e : g.scene.entities)
        if (e.parentId == 0 || !g.scene.byId(e.parentId)) collectVisibleOutliner(e.id, g.outlinerVisibleOrder);
    // drag activation
    if (g.treeDragId && in.mouseDown && !g.treeDragging) {
        if (fabsf(in.mouseX - g.treePressX) + fabsf(in.mouseY - g.treePressY) > 8) g.treeDragging = true;
    }
    g.treeDropId = 0;

    for (const auto& e : g.scene.entities) {
        bool rootOrphan = e.parentId == 0 || !g.scene.byId(e.parentId);
        if (rootOrphan) drawOutlinerNode(ui, e.id, 0);
    }

    if (g.treeDragging) {
        Entity* de = g.scene.byId(g.treeDragId);
        if (de) {
            ui.spacing(2);
            char msg[96];
            snprintf(msg, sizeof(msg), ">> Drag '%s' onto another object to parent it", de->name);
            ui.label(msg, { 1, 0.85f, 0.4f });
        }
    }
    // drop
    if (in.mouseReleased) {
        bool dropped = false;
        // drop su un campo variabile esposta nei Details (assegna il riferimento all'oggetto)
        if (g.treeDragging && !g.treeDropId) {
            DockWindow* dw = g.dock.find("dettagli");
            if (dw && dw->open) {
                for (auto& t : g_varDrops) {
                    if (in.mouseX >= t.x && in.mouseX < t.x + t.w && in.mouseY >= t.y && in.mouseY < t.y + t.h) {
                        Entity* te = g.scene.byId(t.entId);
                        Entity* od = g.scene.byId(g.treeDragId);
                        if (te && od && t.constraintSlot) {
                            if (t.constraintSlot == 1) te->constraintObjA = od->id;
                            else te->constraintObjB = od->id;
                            g.scene.rebuildConstraints();
                            addLog(1, "'%s' assigned as Object %d of the constraint.", od->name, t.constraintSlot);
                        } else if(te&&od&&t.inspectorEvent>=0&&t.inspectorEvent<(int)te->inspectorEvents.size()&&
                           t.inspectorListener>=0&&t.inspectorListener<(int)te->inspectorEvents[t.inspectorEvent].listeners.size()){
                            InspectorEventListener& listener=te->inspectorEvents[t.inspectorEvent].listeners[t.inspectorListener];
                            listener.targetEntity=od->id;listener.callable.clear();listener.arguments.clear();
                            addLog(1,"'%s' assigned as the Inspector Event target.",od->name);
                        } else if (te && od && (t.type == PIN_TRANSFORM || entityMatchesRefClass(*od, t.refClass))) {
                            std::map<std::string, Vec3>* overrides = entityBlueprintOverrides(*te, t.blueprintComponent);
                            if (overrides) (*overrides)[t.var] = { (float)od->id, 0, 0 };
                            addLog(1, "'%s' assigned to variable '%s'.", od ? od->name : "?", t.var);
                        } else if (te && od) {
                            addLog(2, "'%s' is not compatible with '%s' (%s).", od->name, t.var,
                                   refClassLabel(t.refClass).c_str());
                        }
                        dropped = true;
                        break;
                    }
                }
            }
        }
        if (!dropped && g.treeDragging && g.treeDropId && g.treeDropId != g.treeDragId) {
            Entity* child = g.scene.byId(g.treeDragId);
            Entity* parent = g.scene.byId(g.treeDropId);
            if (child && parent && g.scene.setParent(g.treeDragId, g.treeDropId)) {
                addLog(1, "'%s' is now a child of '%s'.", child->name, parent->name);
                g.collapsed.erase(g.treeDropId);
            } else {
                addLog(2, "Not possible: it would create a cycle in the hierarchy.");
            }
        }
        g.treeDragId = 0;
        g.treeDragging = false;
    }

    ui.separator();
    char stats[96];
    snprintf(stats, sizeof(stats), "%d objects, %d selected, %d constraints", (int)g.scene.entities.size(),
             (int)g.selectedIds.size(), (int)g.scene.joints.size());
    ui.label(stats, { 0.55f, 0.59f, 0.66f });

    // Unreal-style object actions live on the Outliner context menu.
    ui.setInteractionBlocked(outlinerWasBlocked);
    const UIInput& menuIn = rawIn;
    Entity* contextEntity = g.scene.byId(g.outlinerContextEntity);
    if (contextEntity) {
        struct ComponentMenuItem { std::string category,label,path; int action=-1; bool disabled=false; };
        std::vector<ComponentMenuItem> components;
        auto component = [&](const char* category,const char* label,int action,bool disabled=false) {
            components.push_back({category,label,{},action,disabled});
        };
        component("Rendering", "Mesh Renderer", ADD_MESH, contextEntity->hasMesh);
        component("Rendering", "Luce puntuale", ADD_LIGHT, contextEntity->isLight);
        component("Rendering", "Camera", ADD_CAMERA, contextEntity->isCamera);
        component("Physics", "Rigid Body", ADD_RIGID_BODY, contextEntity->hasPhysics);
        component("Physics", "Trigger", ADD_TRIGGER, contextEntity->hasTrigger);
        component("Physics", "Joint (nearest object)", ADD_JOINT, g.scene.entities.size()<2);
        component("Physics", "Physics Constraint", ADD_CONSTRAINT, contextEntity->hasConstraint);
        component("Audio", "Audio Source", ADD_AUDIO, contextEntity->hasAudio);
        component("Audio", "Audio Reverb Zone", ADD_REVERB, contextEntity->hasReverb);
        component("AI / Navigation", "AI Agent", ADD_AI_AGENT, contextEntity->hasAIAgent);
        component("AI / Navigation", "Navigation Occluder", ADD_NAV_OCCLUDER, contextEntity->hasNavigationOccluder);
        component("Animation", "Animator", ADD_ANIMATOR, contextEntity->hasAnimator);
        component("Scripting", "Simple script", ADD_SIMPLE_SCRIPT, contextEntity->behavior!=BH_NONE);
        component("Scripting", "Inspector Events", ADD_INSPECTOR_EVENTS, contextEntity->hasInspectorEvents);
        for (const std::string& path : blueprintComponentAssets())
            components.push_back({"Blueprints",fs::path(path).stem().string(),path,-1,false});

        UIRect panel = ui.panelInner(); Renderer* r=ui.r;
        const float menuW=224, rowH=25, menuH=rowH*6+8;
        float menuX=clampf(g.outlinerContextX,panel.x+2,panel.x+panel.w-menuW-2);
        float menuY=clampf(g.outlinerContextY,panel.y+2,panel.y+panel.h-menuH-2);
        UIRect parentMenu{menuX,menuY,menuW,menuH};
        float contentHeight=8; std::string lastCategory;
        for(const auto& item:components){if(item.category!=lastCategory){contentHeight+=21;lastCategory=item.category;}contentHeight+=rowH;}
        float subH=(std::min)(360.0f,(std::max)(80.0f,contentHeight));
        // The submenu is an overlay, not Outliner content: keep it aligned to
        // the first row and open it on the right even when it extends beyond
        // the dock panel into the viewport.
        // Attach the submenu directly to one side of the parent. If there is
        // not enough room on the right, attach it flush to the left edge.
        float subX=menuX+menuW;
        if(subX+252>g.width-2)subX=menuX-252;
        subX=clampf(subX,2.0f,(std::max)(2.0f,(float)g.width-254.0f));
        float subY=clampf(menuY,TOP_H+2,(float)g.height-subH-2);
        UIRect subMenu{subX,subY,252,subH};
        UIRect subBridge{subX<menuX?subX+252:menuX+menuW-1,menuY+4,1,rowH};
        bool insideParent=menuIn.mouseX>=parentMenu.x&&menuIn.mouseX<parentMenu.x+parentMenu.w&&menuIn.mouseY>=parentMenu.y&&menuIn.mouseY<parentMenu.y+parentMenu.h;
        bool insideSub=g.outlinerComponentSubmenu&&menuIn.mouseX>=subMenu.x&&menuIn.mouseX<subMenu.x+subMenu.w&&menuIn.mouseY>=subMenu.y&&menuIn.mouseY<subMenu.y+subMenu.h;
        bool insideBridge=g.outlinerComponentSubmenu&&menuIn.mouseX>=subBridge.x&&menuIn.mouseX<subBridge.x+subBridge.w&&menuIn.mouseY>=subBridge.y&&menuIn.mouseY<subBridge.y+subBridge.h;
        int chosen=-1; bool createPrefab=false,overridePrefab=false;
        std::string chosenBlueprint;
        r->setUIScissor(0,0,0,0,false);
        r->drawRectPx(menuX+4,menuY+5,menuW,menuH,{0,0,0},.35f);
        r->drawRectPx(menuX,menuY,menuW,menuH,{.075f,.084f,.102f},1);
        Entity* prefabRoot=prefabInstanceRoot(*contextEntity);
        const bool canCreate=prefabRoot==nullptr,canOverride=prefabRoot!=nullptr;
        const char* labels[6]={"Add component","Add empty","Add empty parent","Duplicate  Ctrl+D","Create Prefab...","Override Prefab"};
        int parentHover=-1;
        for(int i=0;i<6;i++){
            UIRect row{menuX+4,menuY+4+i*rowH,menuW-8,rowH};
            if(menuIn.mouseX>=row.x&&menuIn.mouseX<row.x+row.w&&menuIn.mouseY>=row.y&&menuIn.mouseY<row.y+row.h)
                parentHover=i;
        }
        if(parentHover==0)g.outlinerComponentSubmenu=true;
        else if(parentHover>0)g.outlinerComponentSubmenu=false;
        for(int i=0;i<6;i++){
            UIRect row{menuX+4,menuY+4+i*rowH,menuW-8,rowH};
            bool hover=menuIn.mouseX>=row.x&&menuIn.mouseX<row.x+row.w&&menuIn.mouseY>=row.y&&menuIn.mouseY<row.y+row.h;
            bool disabled=(i==4&&!canCreate)||(i==5&&!canOverride);
            if((hover&&!disabled)||(i==0&&g.outlinerComponentSubmenu))r->drawRectPx(row.x,row.y,row.w,row.h,{.16f,.29f,.46f},1);
            r->drawTextLine(row.x+10,row.y+4,labels[i],disabled?Vec3{.36f,.39f,.44f}:hover?Vec3{.9f,.96f,1}:Vec3{.82f,.86f,.92f},1);
            if(i==0)r->drawTextLine(row.x+row.w-17,row.y+4,">",{.32f,.68f,1},1);
            if(hover&&!disabled&&menuIn.mouseReleased){
                if(i==1)chosen=-3;else if(i==2)chosen=-4;else if(i==3)chosen=-2;
                else if(i==4)createPrefab=true;else if(i==5)overridePrefab=true;
            }
        }

        if(g.outlinerComponentSubmenu){
            insideSub=menuIn.mouseX>=subMenu.x&&menuIn.mouseX<subMenu.x+subMenu.w&&menuIn.mouseY>=subMenu.y&&menuIn.mouseY<subMenu.y+subMenu.h;
            if(insideSub&&menuIn.wheel!=0){g.outlinerComponentScroll+=menuIn.wheel*32;ui.consumeWheel();}
            float minScroll=(std::min)(0.0f,subH-contentHeight);
            g.outlinerComponentScroll=clampf(g.outlinerComponentScroll,minScroll,0);
            r->drawRectPx(subX+4,subY+5,subMenu.w,subH,{0,0,0},.35f);
            r->drawRectPx(subX,subY,subMenu.w,subH,{.075f,.084f,.102f},1);
            r->setUIScissor(subX,subY,subMenu.w,subH,true);
            float y=subY+4+g.outlinerComponentScroll;lastCategory.clear();
            for(const auto& item:components){
                if(item.category!=lastCategory){
                    UIRect header{subX+4,y,subMenu.w-8,21};
                    r->drawRectPx(header.x,header.y,header.w,header.h,{.105f,.125f,.16f},1);
                    r->drawTextLine(header.x+8,header.y+3,item.category,{.35f,.70f,1},1,.9f);
                    y+=21;lastCategory=item.category;
                }
                UIRect row{subX+4,y,subMenu.w-8,rowH};y+=rowH;
                if(row.y+row.h<subY||row.y>subY+subH)continue;
                bool hover=insideSub&&menuIn.mouseX>=row.x&&menuIn.mouseX<row.x+row.w&&menuIn.mouseY>=row.y&&menuIn.mouseY<row.y+row.h;
                if(hover&&!item.disabled)r->drawRectPx(row.x,row.y,row.w,row.h,{.16f,.29f,.46f},1);
                r->drawTextLine(row.x+12,row.y+4,item.label,item.disabled?Vec3{.36f,.39f,.44f}:hover?Vec3{.9f,.96f,1}:Vec3{.80f,.84f,.90f},1,.92f);
                if(hover&&!item.disabled&&menuIn.mouseReleased){chosen=item.action;chosenBlueprint=item.path;}
            }
            r->setUIScissor(0,0,0,0,false);
            ui.registerBlockingRect(subMenu);
        }
        ui.reclipPanel();ui.registerBlockingRect(parentMenu);
        insideSub=g.outlinerComponentSubmenu&&menuIn.mouseX>=subMenu.x&&menuIn.mouseX<subMenu.x+subMenu.w&&menuIn.mouseY>=subMenu.y&&menuIn.mouseY<subMenu.y+subMenu.h;
        insideBridge=g.outlinerComponentSubmenu&&menuIn.mouseX>=subBridge.x&&menuIn.mouseX<subBridge.x+subBridge.w&&menuIn.mouseY>=subBridge.y&&menuIn.mouseY<subBridge.y+subBridge.h;
        if((menuIn.mousePressed&&!insideParent&&!insideSub&&!insideBridge)||menuIn.keyEscape)g.outlinerContextEntity=0;
        if(chosen==-2){duplicateSceneEntity(contextEntity->id);g.outlinerContextEntity=0;}
        else if(chosen==-3){addEmptyRelativeToEntity(contextEntity->id,false);g.outlinerContextEntity=0;}
        else if(chosen==-4){addEmptyRelativeToEntity(contextEntity->id,true);g.outlinerContextEntity=0;}
        else if(chosen>=0||!chosenBlueprint.empty()){
            addSceneComponent(*contextEntity,chosen,chosenBlueprint);g.outlinerContextEntity=0;g.outlinerComponentSubmenu=false;
        }else if(createPrefab){g.selectedId=contextEntity->id;savePrefab();g.outlinerContextEntity=0;}
        else if(overridePrefab){overridePrefabFromEntity(*contextEntity);g.outlinerContextEntity=0;}
    } else if(g.outlinerContextEntity) g.outlinerContextEntity=0;
}

static void drawAudioAssetDetails(UI& ui) {
    ui.header(g.audioAssetEditKind == 0 ? "AUDIO CLASS" :
              g.audioAssetEditKind == 1 ? "AUDIO ATTENUATION" : "AUDIO CONCURRENCY");
    ui.label(g.audioAssetEditRel, { 0.55f, 0.59f, 0.66f });
    bool changed = false;
    if (g.audioAssetEditKind == 0) {
        changed |= ui.dragFloat("Volume classe (x)", &g.audioClassEdit.volume, 0.01f, 0.0f, 2.0f);
        ui.label("Multiplies the volume of every assigned source.", { 0.55f, 0.59f, 0.66f });
    } else if (g.audioAssetEditKind == 1) {
        changed |= ui.checkbox("3D spatialization", &g.audioAttenuationEdit.spatial);
        if (g.audioAttenuationEdit.spatial) {
            changed |= ui.dragFloat("Min distance", &g.audioAttenuationEdit.minDistance, 0.05f, 0.01f, 1000.0f);
            changed |= ui.dragFloat("Max distance", &g.audioAttenuationEdit.maxDistance, 0.1f, 0.02f, 10000.0f);
            if (g.audioAttenuationEdit.maxDistance < g.audioAttenuationEdit.minDistance)
                g.audioAttenuationEdit.maxDistance = g.audioAttenuationEdit.minDistance;
            static const char* FALLOFF[] = { "Linear", "Inverse", "Exponential" };
            changed |= ui.combo("Attenuation curve", &g.audioAttenuationEdit.falloff, FALLOFF, 3);
        }
        ui.label("The asset overrides the source's local distances.", { 0.55f, 0.59f, 0.66f });
    } else {
        changed |= ui.dragInt("Max voices", &g.audioConcurrencyEdit.maxVoices, 0.15f, 1, 64);
        static const char* RULES[] = { "Reject new", "Stop the oldest" };
        changed |= ui.combo("When full", &g.audioConcurrencyEdit.resolution, RULES, 2);
        ui.label("The limit is shared by every source using this asset.", { 0.55f, 0.59f, 0.66f });
    }
    if ((changed || ui.button("Save asset")) && !saveAudioSettingsAsset())
        addLog(2, "Could not save the audio asset: %s", g.audioAssetEditRel.c_str());
}

static void drawEnumAssetDetails(UI& ui) {
    ui.header("ENUM");
    ui.label(g.enumAssetEditRel, { .55f, .59f, .66f });
    bool changed = false;
    int removeAt = -1;
    for (int i = 0; i < (int)g.enumAssetEdit.values.size(); i++) {
        char value[48];
        snprintf(value, sizeof(value), "%s", g.enumAssetEdit.values[i].c_str());
        char id[48];
        snprintf(id, sizeof(id), "enum_value_%d", i);
        if (ui.textInput(id, value, sizeof(value))) {
            g.enumAssetEdit.values[i] = value[0] ? value : ("Value" + std::to_string(i));
            changed = true;
        }
        if (g.enumAssetEdit.values.size() > 1) {
            snprintf(id, sizeof(id), "Remove##enum%d", i);
            if (ui.buttonColored(id, { .4f, .14f, .14f }, { 1, .85f, .85f })) removeAt = i;
        }
    }
    if (removeAt >= 0) {
        g.enumAssetEdit.values.erase(g.enumAssetEdit.values.begin() + removeAt);
        changed = true;
    }
    if (g.enumAssetEdit.values.size() < 7 && ui.button("+ Add value")) {
        g.enumAssetEdit.values.push_back("Value" + std::to_string(g.enumAssetEdit.values.size()));
        changed = true;
    }
    ui.label("Up to 7 values: compatible with Select and Switch.", { .55f, .59f, .66f });
    if ((changed || ui.button("Save Enum")) && !saveEnumAsset())
        addLog(2, "Could not save the Enum: %s", g.enumAssetEditRel.c_str());
}

enum TransformResetPart { RESET_LOCATION = 1, RESET_ROTATION = 2, RESET_SCALE = 4, RESET_TRANSFORM_ALL = 7 };

static void resetTransformDefaults(Entity& e, int parts) {
    RigidBody* b = e.body;
    if (!b) return;
    Entity* parent = e.parentId ? g.scene.byId(e.parentId) : nullptr;
    Vec3 targetPos = parent ? parent->body->position : Vec3{};
    Quat targetRot = parent ? parent->body->quat : Quat{};
    Vec3 targetScale = parent ? parent->scale : Vec3{1,1,1};
    if (parts & RESET_LOCATION) {
        Vec3 delta = targetPos - b->position;
        b->position = targetPos;
        g.scene.moveDescendants(e.id, delta);
    }
    if (parts & RESET_ROTATION) {
        Quat oldRotation = b->quat;
        b->quat = targetRot;
        g.scene.rotateDescendants(e.id, b->position, oldRotation, targetRot);
        g.inspEuler = {};
        g.inspEulerId = e.id;
    }
    if (parts & RESET_SCALE) {
        Vec3 oldScale = e.scale;
        e.scale = targetScale;
        g.scene.scaleDescendants(e.id, b->position, b->quat, oldScale, targetScale);
    }
    b->velocity = {}; b->angularVelocity = {};
    b->updateInertiaWorld();
    g.scene.syncBodyShape(e);
    if ((e.staticFlags & STATIC_NAVIGATION) || e.hasNavigationOccluder) invalidateNavigation();
}

static void openComponentResetMenu(UI& ui, const Entity& e, int kind) {
    g.componentResetMenuEntity = e.id;
    g.componentResetMenuKind = kind;
    // raw mouse: this may run while the panel is interaction-blocked by the popup
    // (right-clicking another component to move/reopen it), where input() is neutral
    g.componentResetMenuX = ui.rawMouseX();
    g.componentResetMenuY = ui.rawMouseY();
}

static void collectAnimatorControllers(std::vector<std::string>& paths,std::vector<std::string>& labels) {
    paths={""};labels={"None"};
    std::error_code ec;
    for(fs::recursive_directory_iterator it(g.projectDir,fs::directory_options::skip_permission_denied,ec),end;
        !ec&&it!=end;it.increment(ec)){
        if(!it->is_regular_file(ec)||_stricmp(it->path().extension().string().c_str(),".animctrl")!=0)continue;
        std::string rel=fs::relative(it->path(),g.projectDir,ec).string();if(ec){ec.clear();continue;}
        paths.push_back(rel);labels.push_back(it->path().stem().string()+"  ("+rel+")");
    }
}

static void resetComponentDefaults(Entity& e, int kind) {
    Entity defaults;
    RigidBody* b = e.body;
    if (!b) return;
    switch (kind) {
    case DETAIL_TRANSFORM: {
        resetTransformDefaults(e, RESET_TRANSFORM_ALL);
        break;
    }
    case DETAIL_COLLISION:
        e.layer = 0; b->layer = 0; e.collision = e.hasTrigger ? 1 : 0; g.scene.syncBodyShape(e); break;
    case DETAIL_MESH:
        e.mesh = defaults.mesh; e.meshAsset[0] = 0; e.color = defaults.color; e.colorAlpha = defaults.colorAlpha; e.shininess = defaults.shininess;
        e.specular = defaults.specular; e.checker = defaults.checker; e.emissive = defaults.emissive;
        e.doubleSided = defaults.doubleSided;
        g.scene.syncBodyShape(e); break;
    case DETAIL_TRIGGER:
        e.triggerShape = defaults.triggerShape; g.scene.syncBodyShape(e); break;
    case DETAIL_PHYSICS:
        b->type = (e.staticFlags & STATIC_MOVEMENT) ? BodyType::Static : BodyType::Dynamic;
        b->velocity = {}; b->angularVelocity = {}; b->force = {}; b->torque = {};
        b->restitution = .3f; b->friction = .5f; b->linearDamping = .01f; b->angularDamping = .05f;
        b->useGravity = true; b->canSleep = true; b->setMass(b->type == BodyType::Static ? 0.0f : 1.0f);
        g.scene.syncBodyShape(e); break;
    case DETAIL_LIGHT:
        e.lightColor = defaults.lightColor; e.lightIntensity = defaults.lightIntensity; e.lightRange = defaults.lightRange; break;
    case DETAIL_CAMERA:
        e.camFov = defaults.camFov; e.camOffsetY = defaults.camOffsetY;
        e.camLinearClipping=defaults.camLinearClipping; e.camNearClip=defaults.camNearClip;
        e.camClipDistance=defaults.camClipDistance; e.camLayerMask=defaults.camLayerMask; break;
    case DETAIL_AUDIO:
        g.audio.stop(e.id); e.audioClip[0]=e.audioClass[0]=e.audioAttenuation[0]=e.audioConcurrency[0]=0;
        e.audioVolume=defaults.audioVolume; e.audioLoop=defaults.audioLoop; e.audioPlayOnAwake=defaults.audioPlayOnAwake;
        e.audioSpatial=defaults.audioSpatial; e.audioMinDistance=defaults.audioMinDistance; e.audioMaxDistance=defaults.audioMaxDistance; break;
    case DETAIL_REVERB:
        e.reverbRadius=defaults.reverbRadius; e.reverbWet=defaults.reverbWet; e.reverbDecay=defaults.reverbDecay; break;
    case DETAIL_AI_AGENT:
        e.aiSpeed=defaults.aiSpeed; e.aiAcceleration=defaults.aiAcceleration; e.aiAngularSpeed=defaults.aiAngularSpeed;
        e.aiStoppingDistance=defaults.aiStoppingDistance; e.aiBaseOffset=defaults.aiBaseOffset;
        e.aiDebugDraw=defaults.aiDebugDraw;
        e.aiTargetEntity=0; e.aiDestination=b->position;
        e.aiUseTargetEntity=false; e.aiStopped=false; e.aiHasPath=false; e.aiPath.clear(); e.aiPathIndex=0; break;
    case DETAIL_NAV_OCCLUDER:
        e.navigationOccluderPadding=defaults.navigationOccluderPadding; invalidateNavigation(); break;
    case DETAIL_ANIMATOR:
        e.animatorController[0]=0;e.animatorPlayOnAwake=defaults.animatorPlayOnAwake;
        e.animatorSpeed=defaults.animatorSpeed;e.animatorRuntimeState=0;e.animatorRuntimeTime=0;e.animatorRuntimePlaying=false;break;
    case DETAIL_INSPECTOR_EVENTS:
        e.inspectorEvents.clear();{InspectorEventDef event;event.name="Event1";e.inspectorEvents.push_back(std::move(event));}break;
    case DETAIL_SIMPLE_SCRIPT:
        e.behavior=BH_JUMP_SPACE; e.bp[0]=6; e.bp[1]=e.bp[2]=0; break;
    case DETAIL_BLUEPRINT:
        e.varOverrides.clear();e.varAlphaOverrides.clear();
        for (BlueprintComponentDef& component : e.additionalBlueprints) {
            component.varOverrides.clear();
            component.varAlphaOverrides.clear();
        }
        break;
    case DETAIL_JOINTS:
        for (JointDef& j : g.scene.joints) if (j.entA==e.id || j.entB==e.id) {
            Entity* a=g.scene.byId(j.entA); Entity* other=g.scene.byId(j.entB);
            if(a&&other)j.len=a->body->position.distanceTo(other->body->position);
            j.rope=false; j.breakImp=0;
        }
        g.scene.rebuildConstraints(); break;
    case DETAIL_CONSTRAINT: {
        Entity d;   // defaults
        e.constraintObjA = 0; e.constraintObjB = 0; e.conBreak = 0;
        for (int i=0;i<3;i++){ e.conLinMode[i]=d.conLinMode[i]; e.conLinLimit[i]=d.conLinLimit[i];
                               e.conAngMode[i]=d.conAngMode[i]; e.conAngLimit[i]=d.conAngLimit[i]; }
        e.conLinMotor=false; e.conLinMotorTarget={}; e.conLinMotorForce=d.conLinMotorForce;
        e.conAngMotor=false; e.conAngMotorTarget={}; e.conAngMotorForce=d.conAngMotorForce;
        g.scene.rebuildConstraints(); break;
    }
    }
    if ((e.staticFlags & STATIC_NAVIGATION) || e.hasNavigationOccluder) invalidateNavigation();
    addLog(0, "Component reset to its default values: %s.", e.name);
}

static void drawComponentResetMenu(UI& ui, Entity& e) {
    if (g.componentResetMenuEntity != e.id || g.componentResetMenuKind < 0) return;
    const bool transform = g.componentResetMenuKind == DETAIL_TRANSFORM;
    const char* transformItems[] = { "Reset Location", "Reset Rotation", "Reset Scale", "Reset Transform" };
    const int transformParts[] = { RESET_LOCATION, RESET_ROTATION, RESET_SCALE, RESET_TRANSFORM_ALL };
    int count = transform ? 4 : 1;
    float w = 196, itemH = 25, h = count * itemH + 8;
    UIRect panel = ui.panelInner();
    float x = clampf(g.componentResetMenuX, panel.x + 2, panel.x + panel.w - w - 2);
    float y = clampf(g.componentResetMenuY, panel.y + 2, panel.y + panel.h - h - 2);
    UIRect menu{x,y,w,h};
    const UIInput& in = ui.input();
    Renderer* r = ui.r;
    r->setUIScissor(0,0,0,0,false);
    r->drawRectPx(x,y,w,h,{.055f,.062f,.076f},1);
    r->drawRectPx(x,y,w,1,{.30f,.55f,.78f},1);
    bool clicked = false;
    for (int i=0;i<count;i++) {
        UIRect row{x+4,y+4+i*itemH,w-8,itemH};
        bool hover = in.mouseX>=row.x && in.mouseX<row.x+row.w && in.mouseY>=row.y && in.mouseY<row.y+row.h;
        if (hover) r->drawRectPx(row.x,row.y,row.w,row.h,{.12f,.25f,.38f},1);
        const char* label = transform ? transformItems[i] : "Reset componente";
        r->drawTextLine(row.x+8,row.y+4,label,hover?Vec3{.88f,.95f,1}:Vec3{.80f,.84f,.90f},1);
        if (hover && in.mouseReleased) {
            if (transform) {
                resetTransformDefaults(e, transformParts[i]);
                addLog(0, "%s restored: %s.", label + 6, e.name);
            } else resetComponentDefaults(e, g.componentResetMenuKind);
            clicked = true;
        }
    }
    ui.reclipPanel();
    ui.registerBlockingRect(menu);
    bool inside = in.mouseX>=menu.x&&in.mouseX<menu.x+menu.w&&in.mouseY>=menu.y&&in.mouseY<menu.y+menu.h;
    if (clicked || (in.mouseReleased && !inside) || in.keyEscape) {
        g.componentResetMenuEntity = 0;
        g.componentResetMenuKind = -1;
    }
}

// ── Unreal-style big asset picker field (mesh / material) ──
struct AssetPick {
    std::string label;          // display text
    std::string iconImage;      // key into g.assetIconTextures (empty = none)
    GLuint tex = 0;             // explicit texture (material albedo) — overrides iconImage
    Vec3 swatch{ 0, 0, 0 };     // solid-colour thumbnail (material base colour)
    bool useSwatch = false;
};

static void drawAssetThumb(Renderer* r, const AssetPick& p, float x, float y, float s) {
    r->drawRectPx(x, y, s, s, { 0.06f, 0.07f, 0.09f }, 1);
    if (p.tex) r->drawImagePx(p.tex, x + 2, y + 2, s - 4, s - 4, { 1, 1, 1 }, 1);
    else if (p.useSwatch) r->drawRectPx(x + 3, y + 3, s - 6, s - 6, p.swatch, 1);
    else if (!p.iconImage.empty()) {
        auto it = g.assetIconTextures.find(p.iconImage);
        if (it != g.assetIconTextures.end()) r->drawImagePx(it->second, x + 3, y + 3, s - 6, s - 6, { 1, 1, 1 }, 1);
    }
    Vec3 bd{ .30f, .34f, .40f };
    r->drawRectPx(x, y, s, 1, bd, 1); r->drawRectPx(x, y + s - 1, s, 1, bd, 1);
    r->drawRectPx(x, y, 1, s, bd, 1); r->drawRectPx(x + s - 1, y, 1, s, bd, 1);
}

// Draws a large field (thumbnail + name + dropdown arrow). Clicking opens a dropdown
// listing every option (same width as the field, Unreal-style). Returns the newly
// picked index into `options`, or -1 if nothing was picked this frame.
static int drawAssetField(UI& ui, const char* fieldId, const char* label,
                          int current, const std::vector<AssetPick>& options) {
    Renderer* r = ui.r;
    const UIInput& in = ui.input();
    ui.label(label, { 0.55f, 0.59f, 0.66f });
    const float H = 46, TH = H - 10;
    UIRect rc = ui.allocRow(H);
    auto inRect = [&](const UIRect& q) {
        return in.mouseX >= q.x && in.mouseX < q.x + q.w && in.mouseY >= q.y && in.mouseY < q.y + q.h;
    };
    bool overField = inRect(rc);
    r->drawRectPx(rc.x, rc.y, rc.w, rc.h, overField ? Vec3{ .17f, .19f, .23f } : Vec3{ .13f, .145f, .175f }, 1);
    r->drawRectPx(rc.x, rc.y, rc.w, 1, { .28f, .32f, .40f }, .8f);
    const AssetPick* cur = (current >= 0 && current < (int)options.size()) ? &options[current] : nullptr;
    if (cur) drawAssetThumb(r, *cur, rc.x + 5, rc.y + 5, TH);
    else r->drawRectPx(rc.x + 5, rc.y + 5, TH, TH, { .06f, .07f, .09f }, 1);
    std::string name = cur ? cur->label : "None";
    r->drawTextLine(rc.x + TH + 16, rc.y + H * 0.5f - 8, ui.ellipsize(name, rc.w - TH - 46), { .88f, .92f, .98f }, 1);

    bool open = g.assetFieldOpen == fieldId;
    r->drawTextLine(rc.x + rc.w - 18, rc.y + H * 0.5f - 8, open ? "^" : "v", { 0.30f, 0.62f, 0.99f }, 1);
    if (overField && in.mousePressed) { g.assetFieldOpen = open ? std::string() : fieldId; open = !open; }

    int picked = -1;
    float blockBottom = rc.y + rc.h;
    if (open) {
        for (int i = 0; i < (int)options.size(); i++) {
            UIRect irc = ui.allocRow(24);
            blockBottom = irc.y + irc.h;
            bool ihov = inRect(irc);
            r->drawRectPx(irc.x, irc.y, irc.w, irc.h,
                          i == current ? Vec3{ .12f, .24f, .40f } : (ihov ? Vec3{ .20f, .28f, .40f } : Vec3{ .10f, .11f, .135f }), 1);
            drawAssetThumb(r, options[i], irc.x + 3, irc.y + 3, 18);
            r->drawTextLine(irc.x + 28, irc.y + 4, ui.ellipsize(options[i].label, irc.w - 40), { .85f, .9f, .97f }, 1);
            if (ihov && in.mousePressed) { picked = i; g.assetFieldOpen.clear(); }
        }
        // click anywhere outside the field+dropdown closes it
        bool insideBlock = in.mouseX >= rc.x && in.mouseX < rc.x + rc.w && in.mouseY >= rc.y && in.mouseY < blockBottom;
        if (in.mousePressed && picked < 0 && !insideBlock) g.assetFieldOpen.clear();
    }
    return picked;
}

static void drawDetailsContent(UI& ui) {
    g_varDrops.clear();   // drop target ricostruiti ogni volta che i Details si disegnano
    Entity* sel = g.scene.byId(g.selectedId);
    if (!sel) {
        // A component context menu belongs to the selected entity. Do not
        // leave it armed when the selection is cleared (or an asset replaces
        // the entity Details), otherwise returning to that entity would block
        // the panel again with a stale popup.
        g.componentResetMenuEntity = 0;
        g.componentResetMenuKind = -1;
        if (!g.enumAssetEditRel.empty()) {
            drawEnumAssetDetails(ui);
            return;
        }
        if (g.audioAssetEditKind >= 0 && !g.audioAssetEditRel.empty()) {
            drawAudioAssetDetails(ui);
            return;
        }
        ui.spacing(10);
        ui.label("No object selected.", { 0.55f, 0.59f, 0.66f });
        ui.label("Click in the viewport or in the Outliner.", { 0.55f, 0.59f, 0.66f });
        return;
    }
    Entity& e = *sel;
    RigidBody* b = e.body;
    if (g.componentResetMenuEntity != 0 && g.componentResetMenuEntity != e.id) {
        g.componentResetMenuEntity = 0;
        g.componentResetMenuKind = -1;
    }
    bool detailsWasBlocked = ui.interactionBlocked();
    bool resetMenuBlocks = g.componentResetMenuEntity == e.id && g.componentResetMenuKind >= 0;
    if (resetMenuBlocks) ui.setInteractionBlocked(true);
    // While the reset popup blocks the panel, still let a right-click on a component
    // header reposition it (same component) or reopen it on another component. Only
    // when the panel isn't covered by some other window (detailsWasBlocked).
    if (resetMenuBlocks && !detailsWasBlocked) ui.setComponentResetProbe(true);

    if (g.staticMenuEntity != e.id) { g.staticMenuEntity = e.id; g.staticMenuOpen = false; }
    ui.row(2);
    ui.textInput("nome", e.name, sizeof(e.name));
    bool anyStatic = e.staticFlags != 0;
    if (ui.buttonColored(g.staticMenuOpen ? "Static  ^" : (anyStatic ? "Static [x]  v" : "Static  v"),
                         anyStatic ? Vec3{ .12f, .32f, .50f } : Vec3{ .16f, .18f, .22f },
                         anyStatic ? Vec3{ .78f, .92f, 1.0f } : Vec3{ .84f, .87f, .92f }))
        g.staticMenuOpen = !g.staticMenuOpen;
    if (g.staticMenuOpen) {
        ui.header("STATIC FLAGS");
        bool movement = (e.staticFlags & STATIC_MOVEMENT) != 0;
        bool lighting = (e.staticFlags & STATIC_LIGHTING) != 0;
        bool navigation = (e.staticFlags & STATIC_NAVIGATION) != 0;
        if (ui.checkbox("Static movement", &movement)) {
            if (movement) {
                e.staticFlags |= STATIC_MOVEMENT;
                b->type = BodyType::Static; b->velocity = {}; b->angularVelocity = {}; b->setMass(0);
            } else e.staticFlags &= ~STATIC_MOVEMENT;
        }
        if (ui.checkbox("Static lighting (Light Bake)", &lighting))
            e.staticFlags = lighting ? e.staticFlags | STATIC_LIGHTING : e.staticFlags & ~STATIC_LIGHTING;
        if (ui.checkbox("Static navigation (Nav Bake)", &navigation)) {
            e.staticFlags = navigation ? e.staticFlags | STATIC_NAVIGATION : e.staticFlags & ~STATIC_NAVIGATION;
            g.navigation.baked = false;
            g.navigation.status = "Scene changed: run the Bake again.";
        }
        ui.label("Ogni flag e' indipendente e viene usato dal relativo baker.", { .55f, .59f, .66f });
    }
    ui.spacing(3);
    ui.header("TAGS");
    int removeTag = -1;
    for (int i = 0; i < (int)e.tags.size(); i++) {
        char value[48]; snprintf(value, sizeof(value), "%s", e.tags[i].c_str());
        char inputId[48]; snprintf(inputId, sizeof(inputId), "tag_value_%d", i);
        ui.row(2);
        if (ui.textInput(inputId, value, sizeof(value))) {
            for (char* c = value; *c; c++) if (*c == ' ') *c = '_';
            e.tags[i] = value;
        }
        char removeId[48]; snprintf(removeId, sizeof(removeId), "-##tag_remove_%d", i);
        if (ui.buttonColored(removeId, { .40f, .14f, .14f }, { 1, .84f, .84f })) removeTag = i;
    }
    if (removeTag >= 0) e.tags.erase(e.tags.begin() + removeTag);
    if (e.tags.size() < 16 && ui.button("+ Add Tag"))
        e.tags.push_back("Tag" + std::to_string(e.tags.size() + 1));
    ui.label("Use Find Actor by Tag for references without coupling.", { .55f, .59f, .66f });
    if (e.parentId != 0) {
        Entity* par = g.scene.byId(e.parentId);
        char pl[80];
        snprintf(pl, sizeof(pl), "Child of: %s", par ? par->name : "?");
        ui.label(pl, { 0.55f, 0.59f, 0.66f });
        if (ui.button("Stacca dal padre")) {
            g.scene.setParent(e.id, 0);
            addLog(0, "'%s' staccato dal padre.", e.name);
        }
    }
    ui.spacing(2);

    // parented objects edit their transform RELATIVE to the parent (like Unreal)
    Entity* tpar = e.parentId ? g.scene.byId(e.parentId) : nullptr;
    RigidBody* pb = tpar ? tpar->body : nullptr;

    ui.spacing(6);
    bool transformCollapsed = (e.detailCollapsed & (1u << DETAIL_TRANSFORM)) != 0;
    int transformHeader = ui.componentBegin("detail_transform", "TRANSFORM", transformCollapsed, false,
                                            false, false, false);
    if (transformHeader & UI::COMP_TOGGLED) {
        e.detailCollapsed ^= 1u << DETAIL_TRANSFORM;
        transformCollapsed = !transformCollapsed;
    }
    if (transformHeader & UI::COMP_RESET) openComponentResetMenu(ui, e, DETAIL_TRANSFORM);
    if (!transformCollapsed) {
    ui.label(pb ? "Position (relative)" : "Position", { 0.55f, 0.59f, 0.66f });
    ui.row(3);
    // shown position: local if parented, world otherwise
    Vec3 pos = pb ? divComponents(pb->quat.conjugate().rotate(b->position - pb->position), tpar->scale) : b->position;
    bool posCh = false;
    posCh |= ui.dragFloat("X##px", &pos.x, 0.03f, -100000, 100000);
    posCh |= ui.dragFloat("Y##py", &pos.y, 0.03f, -100000, 100000);
    posCh |= ui.dragFloat("Z##pz", &pos.z, 0.03f, -100000, 100000);
    if (posCh) {
        Vec3 newWorld = pb ? pb->position + pb->quat.rotate(mulComponents(pos, tpar->scale)) : pos;
        Vec3 d = newWorld - b->position;
        b->position = newWorld;
        b->velocity = {};
        b->updateAABB();
        b->wake();
        g.scene.moveDescendants(e.id, d);
        if ((e.staticFlags & STATIC_NAVIGATION) || e.hasNavigationOccluder) invalidateNavigation();
    }
    ui.label(pb ? "Relative rotation (degrees)" : "Rotation (degrees)", { 0.55f, 0.59f, 0.66f });
    // keep the Euler angles the user is editing stable: only re-read them from the
    // (relative) quaternion when the selection changes or something else rotated it.
    {
        Quat shown = pb ? pb->quat.conjugate() * b->quat : b->quat;
        Quat expected = Quat::fromEulerDeg(g.inspEuler.x, g.inspEuler.y, g.inspEuler.z);
        float dot = expected.x * shown.x + expected.y * shown.y +
                    expected.z * shown.z + expected.w * shown.w;
        if (g.inspEulerId != e.id || fabsf(dot) < 0.99999f) {
            g.inspEuler = quatToEulerDeg(shown);
            g.inspEulerId = e.id;
        }
    }
    ui.row(3);
    bool rotCh = false;
    rotCh |= ui.dragFloat("X##rx", &g.inspEuler.x, 0.5f, -3600, 3600);
    rotCh |= ui.dragFloat("Y##ry", &g.inspEuler.y, 0.5f, -3600, 3600);
    rotCh |= ui.dragFloat("Z##rz", &g.inspEuler.z, 0.5f, -3600, 3600);
    if (rotCh) {
        Quat local = Quat::fromEulerDeg(g.inspEuler.x, g.inspEuler.y, g.inspEuler.z).normalized();
        Quat oldRotation = b->quat;
        Quat newRotation = pb ? (pb->quat * local).normalized() : local;
        b->quat = newRotation;
        b->updateInertiaWorld();
        b->updateAABB();
        b->wake();
        g.scene.rotateDescendants(e.id, b->position, oldRotation, newRotation);
        if ((e.staticFlags & STATIC_NAVIGATION) || e.hasNavigationOccluder) invalidateNavigation();
    }
    ui.label("X rolls | Y pitches | Z yaws", { 0.5f, 0.54f, 0.6f });
    ui.label(pb ? "Scale (relative)" : "Scale", { 0.55f, 0.59f, 0.66f });
    ui.row(3);
    Vec3 shownScale = pb ? divComponents(e.scale, tpar->scale) : e.scale;
    bool sclCh = false;
    sclCh |= ui.dragFloat("X##sx", &shownScale.x, 0.02f, 0.05f, 60);
    sclCh |= ui.dragFloat("Y##sy", &shownScale.y, 0.02f, 0.05f, 60);
    sclCh |= ui.dragFloat("Z##sz", &shownScale.z, 0.02f, 0.05f, 60);
    if (sclCh) {
        float oldExtent=b->position.y-b->aabb.min.y;
        Vec3 oldScale=e.scale;
        e.scale = pb ? mulComponents(shownScale, tpar->scale) : shownScale;
        g.scene.scaleDescendants(e.id, b->position, b->quat, oldScale, e.scale);
        g.scene.syncBodyShape(e);
        if(e.hasAIAgent)e.aiBaseOffset=(std::max)(0.0f,e.aiBaseOffset+(b->position.y-b->aabb.min.y)-oldExtent);
        if ((e.staticFlags & STATIC_NAVIGATION) || e.hasNavigationOccluder) invalidateNavigation();
    }
    }
    ui.componentEnd();

    // ── COLLISIONI: sezione fissa sotto la trasformazione (stile Unreal) ──
    if (e.hasMesh || e.hasTrigger) {
    ui.spacing(6);
    bool collisionCollapsed = (e.detailCollapsed & (1u << DETAIL_COLLISION)) != 0;
    int collisionHeader = ui.componentBegin("detail_collision", "COLLISIONS", collisionCollapsed, false,
                                            false, false, false);
    if (collisionHeader & UI::COMP_TOGGLED) {
        e.detailCollapsed ^= 1u << DETAIL_COLLISION;
        collisionCollapsed = !collisionCollapsed;
    }
    if (collisionHeader & UI::COMP_RESET) openComponentResetMenu(ui, e, DETAIL_COLLISION);
    if (!collisionCollapsed) {
    {
        // collision layer (Unity-style): chi puo' interagire con chi (matrice in Impostazioni)
        std::vector<const char*> ln;
        for (int i = 0; i < g.scene.layers.count; i++) ln.push_back(g.scene.layers.names[i]);
        int lay = (e.layer >= 0 && e.layer < g.scene.layers.count) ? e.layer : 0;
        if (!ln.empty() && ui.combo("Layer", &lay, ln.data(), (int)ln.size())) {
            e.layer = lay;
            b->layer = lay;
        }
    }
    if (e.hasTrigger) {
        static const char* TRIGGER_SHAPES[] = { "Box", "Sphere", "Capsule" };
        char cb[96];
        snprintf(cb, sizeof(cb), "Source: Trigger (%s)", TRIGGER_SHAPES[e.triggerShape]);
        ui.label(cb, { 0.55f, 0.72f, 0.62f });
        ui.label("Response: Overlap (Begin/End Overlap), no hit.", { 0.5f, 0.54f, 0.6f });
    } else {
        static const char* COLLIDER_NAMES[] = { "Box", "Sphere", "Cylinder", "Cone", "Capsule" };
        const char* cs = COLLIDER_NAMES[(int)e.mesh >= 0 && (int)e.mesh < MESH_COUNT ? (int)e.mesh : 0];
        char cb[80];
        snprintf(cb, sizeof(cb), "Collider: %s (follows mesh and scale)", cs);
        ui.label(cb, { 0.5f, 0.54f, 0.6f });
    }
    if (!e.hasTrigger)
        ui.label(e.hasPhysics ? "Response: solid (Hit)." : "Response: Mesh Overlap (Begin/End Overlap).",
                 { 0.5f, 0.54f, 0.6f });
    }
    ui.componentEnd();
    }

    // ── components (Unity-style: removable via the x, addable below) ──
    if (g.detailsDragEntity != e.id && !ui.input().mouseDown) {
        g.detailsDragEntity = 0;
        g.detailsDragComponent = -1;
    }
    g.detailCards.clear();
    // Records a card's slot so the drop indicator can snap between cards, and
    // starts a drag (capturing where inside the header the cursor grabbed it).
    auto beginCardDrag = [&](int kind, int bpIndex, int flags, const char* title) {
        UIRect hr = ui.lastComponentHeader();
        g.detailCards.push_back({ kind, bpIndex, hr.y, hr.h });
        if (flags & UI::COMP_PRESSED) {
            g.detailsDragEntity = e.id;
            g.detailsDragComponent = kind;
            g.detailsDragBpIndex = bpIndex;
            g.detailsDragGrabDY = ui.input().mouseY - hr.y;
            g.detailsDragCardH = hr.h;
            g.detailsDragTitle = title ? title : "";
        }
    };
    auto optionalHeader = [&](int kind, const char* id, const char* title, bool removable, bool& collapsed) {
        ui.spacing(6);
        bool dragging = g.detailsDragEntity == e.id && g.detailsDragComponent == kind &&
                        g.detailsDragBpIndex < 0 && ui.input().mouseDown;
        int flags = ui.componentBegin(id, title, collapsed, removable, dragging, false, true);
        if (flags & UI::COMP_TOGGLED) {
            e.detailCollapsed ^= 1u << kind;
            collapsed = !collapsed;
        }
        if (flags & UI::COMP_RESET) openComponentResetMenu(ui, e, kind);
        beginCardDrag(kind, -1, flags, title);
        return flags;
    };

    for (int detailKind : e.detailOrder) {
    if (detailKind == DETAIL_MESH && e.hasMesh) {
        bool collapsed = (e.detailCollapsed & (1u << DETAIL_MESH)) != 0;
        int card = optionalHeader(DETAIL_MESH, "detail_mesh", "MESH RENDERER", true, collapsed);
        if (card & UI::COMP_REMOVE) {
            e.hasMesh = false;
            g.scene.syncBodyShape(e);
            addLog(0, "Mesh component removed from %s: Collisions does not create implicit shapes.", e.name);
        } else if (!collapsed) {
            // ── Mesh (shape / model): big Unreal-style picker with a mini preview ──
            std::vector<AssetPick> meshOpts;
            const char* primIcons[5] = { "ent_cube", "ent_sphere", "ent_cylinder", "ent_cone", "ent_capsule" };
            const char* primNames[5] = { "Cube", "Sphere", "Cylinder", "Cone", "Capsule" };
            for (int i = 0; i < 5; i++) { AssetPick p; p.label = primNames[i]; p.iconImage = primIcons[i]; meshOpts.push_back(p); }
            for (const std::string& asset : g.projectMeshAssets) {
                AssetPick p; p.label = fs::path(asset).stem().string(); p.iconImage = "ent_mesh"; meshOpts.push_back(p);
            }
            int meshCur = (int)e.mesh;
            if (e.meshAsset[0]) {
                auto found = std::find_if(g.projectMeshAssets.begin(), g.projectMeshAssets.end(),
                                          [&](const std::string& p) { return _stricmp(p.c_str(), e.meshAsset) == 0; });
                meshCur = found == g.projectMeshAssets.end() ? 0 : 5 + (int)(found - g.projectMeshAssets.begin());
            }
            int pickedMesh = drawAssetField(ui, "mesh_field", "Shape / Model", meshCur, meshOpts);
            if (pickedMesh >= 0) {
                float oldExtent = b->position.y - b->aabb.min.y;
                if (pickedMesh < 5) { e.mesh = (MeshType)pickedMesh; e.meshAsset[0] = 0; }
                else {
                    e.mesh = MESH_CUBE;   // primitive collision fallback until a custom collider is selected
                    snprintf(e.meshAsset, sizeof(e.meshAsset), "%s", g.projectMeshAssets[pickedMesh - 5].c_str());
                }
                g.scene.syncBodyShape(e);
                if (e.hasAIAgent) e.aiBaseOffset = (std::max)(0.0f, e.aiBaseOffset + (b->position.y - b->aabb.min.y) - oldExtent);
                if ((e.staticFlags & STATIC_NAVIGATION) || e.hasNavigationOccluder) invalidateNavigation();
            }

            // ── Material: big Unreal-style picker; thumbnail = material preview ──
            std::vector<AssetPick> matOpts;
            AssetPick none; none.label = "None"; matOpts.push_back(none);
            for (const std::string& m : g.projectMaterialAssets) {
                AssetPick p; p.label = fs::path(m).stem().string();
                MaterialEval ev; GLuint tex = 0; resolveMaterial(m, ev, tex);
                if (tex) p.tex = tex; else { p.useSwatch = true; p.swatch = ev.baseColor; }
                matOpts.push_back(p);
            }
            int matCur = 0;
            if (e.materialAsset[0]) {
                auto found = std::find_if(g.projectMaterialAssets.begin(), g.projectMaterialAssets.end(),
                                          [&](const std::string& p) { return _stricmp(p.c_str(), e.materialAsset) == 0; });
                matCur = found == g.projectMaterialAssets.end() ? 0 : 1 + (int)(found - g.projectMaterialAssets.begin());
            }
            int pickedMat = drawAssetField(ui, "mat_field", "Material", matCur, matOpts);
            if (pickedMat >= 0) {
                if (pickedMat <= 0) e.materialAsset[0] = 0;
                else snprintf(e.materialAsset, sizeof(e.materialAsset), "%s", g.projectMaterialAssets[pickedMat - 1].c_str());
            }
            if (e.materialAsset[0] && ui.button("Open material editor"))
                openMaterialDoc(g.projectDir + "\\" + e.materialAsset, e.materialAsset);
        }
        ui.componentEnd();
    }

    if (detailKind == DETAIL_TRIGGER && e.hasTrigger) {
        bool collapsed = (e.detailCollapsed & (1u << DETAIL_TRIGGER)) != 0;
        int card = optionalHeader(DETAIL_TRIGGER, "detail_trigger", "TRIGGER", true, collapsed);
        if (card & UI::COMP_REMOVE) {
            e.hasTrigger = false;
            g.scene.syncBodyShape(e);
            addLog(0, "Trigger component removed from %s.", e.name);
        } else if (!collapsed) {
            static const char* SHAPES[] = { "Box", "Sphere", "Capsule" };
            int shape = e.triggerShape;
            if (ui.combo("Forma", &shape, SHAPES, 3)) {
                e.triggerShape = shape;
                g.scene.syncBodyShape(e);
            }
            ui.label("Uses the Transform's position, rotation and scale.", { .5f, .54f, .6f });
            if (e.hasMesh) ui.label("The Trigger takes priority as the collider; the Mesh stays visible.", { .78f, .62f, .34f });
        }
        ui.componentEnd();
    }

    if (detailKind == DETAIL_PHYSICS && e.hasPhysics) {
        bool collapsed = (e.detailCollapsed & (1u << DETAIL_PHYSICS)) != 0;
        int card = optionalHeader(DETAIL_PHYSICS, "detail_physics", "PHYSICS (RIGID BODY)", true, collapsed);
        if (card & UI::COMP_REMOVE) {
            e.hasPhysics = false;
            b->velocity = {};
            b->angularVelocity = {};
            g.scene.syncBodyShape(e);
            addLog(0, "Physics component removed from %s.", e.name);
        } else if (!collapsed) {
            static const char* TYPES[] = { "Dynamic", "Static" };
            int type = b->type == BodyType::Static ? 1 : 0;
            if (ui.combo("Type", &type, TYPES, 2)) {
                b->type = type ? BodyType::Static : BodyType::Dynamic;
                b->velocity = {};
                b->angularVelocity = {};
                b->setMass(type ? 0 : (b->mass > 0 ? b->mass : 1));
            }
            if (b->type == BodyType::Dynamic) {
                float mass = b->mass;
                if (ui.dragFloat("Massa (kg)", &mass, 0.05f, 0.05f, 5000)) b->setMass(mass);
                bool grav = b->useGravity;
                if (ui.checkbox("Gravity", &grav)) {
                    b->useGravity = grav;
                    b->wake();
                }
            }
            float fric = b->friction, rest = b->restitution;
            if (ui.dragFloat("Friction", &fric, 0.005f, 0, 2)) b->friction = fric;
            if (ui.dragFloat("Bounciness", &rest, 0.005f, 0, 1)) b->restitution = rest;
            ui.dragFloat("Linear damping", &b->linearDamping, 0.002f, 0, 5);
            ui.dragFloat("Freno angolare", &b->angularDamping, 0.002f, 0, 5);
        }
        ui.componentEnd();
    }

    if (detailKind == DETAIL_LIGHT && e.isLight) {
        bool collapsed = (e.detailCollapsed & (1u << DETAIL_LIGHT)) != 0;
        int card = optionalHeader(DETAIL_LIGHT, "detail_light", "LUCE PUNTUALE", true, collapsed);
        if (card & UI::COMP_REMOVE) {
            e.isLight = false;
            addLog(0, "Light component removed from %s.", e.name);
        } else if (!collapsed) {
            ui.colorEdit("Colore luce", &e.lightColor);
            ui.dragFloat("Intensity", &e.lightIntensity, 0.03f, 0, 20);
            ui.dragFloat("Radius", &e.lightRange, 0.1f, 0.5f, 60);
        }
        ui.componentEnd();
    }

    if (detailKind == DETAIL_CAMERA && e.isCamera) {
        bool collapsed = (e.detailCollapsed & (1u << DETAIL_CAMERA)) != 0;
        int card = optionalHeader(DETAIL_CAMERA, "detail_camera", "CAMERA", true, collapsed);
        if (card & UI::COMP_REMOVE) {
            e.isCamera = false;
            addLog(0, "Camera component removed from %s.", e.name);
        } else if (!collapsed) {
            ui.dragFloat("FOV (degrees)", &e.camFov, 0.2f, 20, 140);
            ui.dragFloat("Eye height", &e.camOffsetY, 0.02f, -5, 10);
            ui.checkbox("Near Clipping", &e.camLinearClipping);
            if(e.camLinearClipping){
                ui.dragFloat("Near Distance",&e.camNearClip,.005f,.001f,1000.0f);
                if(ui.dragFloat("Far Distance",&e.camClipDistance,.5f,.011f,100000.0f))
                    e.camClipDistance=(std::max)(e.camClipDistance,e.camNearClip+.01f);
            }
            ui.header("CULLING LAYERS");
            for(int layerIndex=0;layerIndex<g.scene.layers.count;layerIndex++){
                bool visible=(e.camLayerMask&(1u<<layerIndex))!=0;
                char layerLabel[64];snprintf(layerLabel,sizeof(layerLabel),"%s##camera_layer_%d",
                    g.scene.layers.names[layerIndex][0]?g.scene.layers.names[layerIndex]:"Layer",layerIndex);
                if(ui.checkbox(layerLabel,&visible)){
                    if(visible)e.camLayerMask|=1u<<layerIndex;else e.camLayerMask&=~(1u<<layerIndex);
                }
            }
            ui.label("In Play the view starts from here.", { 0.55f, 0.59f, 0.66f });
        }
        ui.componentEnd();
    }

    if (detailKind == DETAIL_AUDIO && e.hasAudio) {
        bool collapsed = (e.detailCollapsed & (1u << DETAIL_AUDIO)) != 0;
        int card = optionalHeader(DETAIL_AUDIO, "detail_audio", "AUDIO SOURCE", true, collapsed);
        if (card & UI::COMP_REMOVE) {
            g.audio.stop(e.id);
            e.hasAudio = false;
            e.audioClip[0] = 0;
            addLog(0, "Audio Source component removed from %s.", e.name);
        } else if (!collapsed) {
            ui.label(e.audioClip[0] ? std::string("Clip: ") + e.audioClip : "Clip: Nessuno  [trascina un audio qui]",
                     e.audioClip[0] ? Vec3{ 0.9f, 0.65f, 0.82f } : Vec3{ 0.55f, 0.59f, 0.66f });
            ui.row(2);
            if (ui.button("Play")) {
                g.audio.stop(e.id);
                if (!playAudioSource(e) && !e.audioClip[0]) addLog(2, "Assign an audio clip first.");
            }
            if (ui.button("Stop")) g.audio.stop(e.id);
            if (ui.dragFloat("Volume (x)", &e.audioVolume, 0.01f, 0, 2))
                setAudioSourceVolume(e, e.audioVolume);
            ui.label(e.audioClass[0] ? std::string("Audio Class: ") + e.audioClass : "Audio Class: None  [drag here]",
                     e.audioClass[0] ? Vec3{ .45f, .78f, 1.0f } : Vec3{ .55f, .59f, .66f });
            if (e.audioClass[0] && ui.button("Remove Audio Class")) e.audioClass[0] = 0;
            ui.label(e.audioConcurrency[0] ? std::string("Concurrency: ") + e.audioConcurrency : "Concurrency: None  [drag here]",
                     e.audioConcurrency[0] ? Vec3{ 1.0f, .68f, .35f } : Vec3{ .55f, .59f, .66f });
            if (e.audioConcurrency[0] && ui.button("Remove Concurrency")) e.audioConcurrency[0] = 0;
            ui.checkbox("Play on Awake", &e.audioPlayOnAwake);
            ui.checkbox("Loop", &e.audioLoop);
            ui.label(e.audioAttenuation[0] ? std::string("Attenuation: ") + e.audioAttenuation : "Attenuation: local  [drag an asset here]",
                     e.audioAttenuation[0] ? Vec3{ .72f, .48f, 1.0f } : Vec3{ .55f, .59f, .66f });
            if (e.audioAttenuation[0]) {
                if (ui.button("Use local attenuation")) e.audioAttenuation[0] = 0;
            } else {
                ui.checkbox("3D spatial", &e.audioSpatial);
                if (e.audioSpatial) {
                    ui.dragFloat("Min distance", &e.audioMinDistance, 0.05f, 0.01f, 1000);
                    if (ui.dragFloat("Max distance", &e.audioMaxDistance, 0.1f, 0.02f, 10000) &&
                        e.audioMaxDistance < e.audioMinDistance) e.audioMaxDistance = e.audioMinDistance;
                }
            }
            if (e.audioClip[0] && ui.button("Remove clip")) {
                g.audio.stop(e.id);
                e.audioClip[0] = 0;
            }
        }
        ui.componentEnd();
    }

    if (detailKind == DETAIL_REVERB && e.hasReverb) {
        bool collapsed = (e.detailCollapsed & (1u << DETAIL_REVERB)) != 0;
        int card = optionalHeader(DETAIL_REVERB, "detail_reverb", "AUDIO REVERB ZONE", true, collapsed);
        if (card & UI::COMP_REMOVE) {
            e.hasReverb = false;
            addLog(0, "Audio Reverb Zone removed from %s.", e.name);
        } else if (!collapsed) {
            ui.dragFloat("Zone radius", &e.reverbRadius, 0.05f, 0.1f, 1000.0f);
            ui.dragFloat("Wet Level", &e.reverbWet, 0.01f, 0.0f, 1.0f);
            ui.dragFloat("Decay (seconds)", &e.reverbDecay, 0.02f, 0.1f, 12.0f);
            ui.label("A listener inside the sphere gets the tail and ambient diffusion.", { .55f, .59f, .66f });
        }
        ui.componentEnd();
    }

    if (detailKind == DETAIL_AI_AGENT && e.hasAIAgent) {
        bool collapsed = (e.detailCollapsed & (1u << DETAIL_AI_AGENT)) != 0;
        int card = optionalHeader(DETAIL_AI_AGENT, "detail_ai_agent", "AI AGENT", true, collapsed);
        if (card & UI::COMP_REMOVE) {
            e.hasAIAgent = false; e.aiHasPath = false; e.aiPath.clear();
            addLog(0, "AI Agent removed from %s.", e.name);
        } else if (!collapsed) {
            ui.dragFloat("Speed", &e.aiSpeed, 0.03f, 0.0f, 100.0f);
            ui.dragFloat("Acceleration", &e.aiAcceleration, 0.05f, 0.0f, 500.0f);
            ui.dragFloat("Rotation speed", &e.aiAngularSpeed, 1.0f, 0.0f, 1440.0f);
            ui.dragFloat("Stopping Distance", &e.aiStoppingDistance, 0.01f, 0.0f, 50.0f);
            ui.dragFloat("Base Offset da terra", &e.aiBaseOffset, 0.01f, 0.0f, 100.0f);
            ui.checkbox("Show path debug", &e.aiDebugDraw);
            bool stopped = e.aiStopped;
            if (ui.checkbox("Is Stopped", &stopped)) bpAISetStoppedCb(&e, stopped);
            std::vector<const char*> targetNames{ "None (uses Destination)" };
            std::vector<int> targetIds{ 0 };
            int selectedTarget = 0;
            for (Entity& other : g.scene.entities) {
                if (other.id == e.id) continue;
                targetIds.push_back(other.id); targetNames.push_back(other.name);
                if (other.id == e.aiTargetEntity && e.aiUseTargetEntity) selectedTarget = (int)targetIds.size() - 1;
            }
            if (ui.combo("Target", &selectedTarget, targetNames.data(), (int)targetNames.size())) {
                if (selectedTarget == 0) { e.aiUseTargetEntity = false; e.aiTargetEntity = 0; e.aiHasPath = false; }
                else aiSetTarget(e, targetIds[selectedTarget]);
            }
            if (!e.aiUseTargetEntity) {
                ui.label("Destination", { .55f, .59f, .66f });
                ui.row(3);
                bool changed = false;
                changed |= ui.dragFloat("X##aidest", &e.aiDestination.x, 0.05f, -100000, 100000);
                changed |= ui.dragFloat("Y##aidest", &e.aiDestination.y, 0.05f, -100000, 100000);
                changed |= ui.dragFloat("Z##aidest", &e.aiDestination.z, 0.05f, -100000, 100000);
                if (changed) { e.aiHasPath = false; e.aiRepathTimer = 0; }
            }
            char state[128];
            snprintf(state, sizeof(state), "%s | distance %.2f m", e.aiHasPath ? "Active path" : "No path", e.aiRemainingDistance);
            ui.label(state, e.aiHasPath ? Vec3{ .45f, .95f, .62f } : Vec3{ .65f, .68f, .74f });
            if (!g.navigation.baked && ui.button("Open Navigation / Bake")) {
                DockWindow* navigation = g.dock.find("navigation");
                if (navigation) navigation->open = true;
            }
        }
        ui.componentEnd();
    }

    if (detailKind == DETAIL_NAV_OCCLUDER && e.hasNavigationOccluder) {
        bool collapsed = (e.detailCollapsed & (1u << DETAIL_NAV_OCCLUDER)) != 0;
        int card = optionalHeader(DETAIL_NAV_OCCLUDER, "detail_nav_occluder", "NAVIGATION OCCLUDER", true, collapsed);
        if (card & UI::COMP_REMOVE) {
            e.hasNavigationOccluder = false;
            invalidateNavigation();
            addLog(0, "Navigation Occluder removed from %s.", e.name);
        } else if (!collapsed) {
            if (ui.dragFloat("Obstacle padding", &e.navigationOccluderPadding, 0.01f, 0.0f, 20.0f)) invalidateNavigation();
            ui.label("Subtracts the Mesh/Trigger shape from the NavMesh on the next Bake.", { .55f, .59f, .66f });
            if (!e.hasMesh && !e.hasTrigger)
                ui.label("Add a Mesh Renderer or Trigger to define the shape.", { .95f, .58f, .28f });
        }
        ui.componentEnd();
    }

    if (detailKind == DETAIL_ANIMATOR && e.hasAnimator) {
        bool collapsed=(e.detailCollapsed&(1u<<DETAIL_ANIMATOR))!=0;
        int card=optionalHeader(DETAIL_ANIMATOR,"detail_animator","ANIMATOR",true,collapsed);
        if(card&UI::COMP_REMOVE){
            e.hasAnimator=false;e.animatorController[0]=0;e.animatorRuntimePlaying=false;
            addLog(0,"Animator component removed from %s.",e.name);
        }else if(!collapsed){
            std::vector<std::string> paths,labels;collectAnimatorControllers(paths,labels);
            std::vector<const char*> names;for(std::string& label:labels)names.push_back(label.c_str());
            int selected=0;for(int i=0;i<(int)paths.size();i++)if(paths[i]==e.animatorController)selected=i;
            if(ui.combo("Controller",&selected,names.data(),(int)names.size())){
                snprintf(e.animatorController,sizeof(e.animatorController),"%s",paths[selected].c_str());
                e.animatorRuntimeState=0;e.animatorRuntimeTime=0;e.animatorRuntimePlaying=false;
            }
            ui.checkbox("Play on Awake",&e.animatorPlayOnAwake);
            ui.dragFloat("Speed",&e.animatorSpeed,.01f,0,20);
            if(e.animatorController[0])ui.label(std::string("Asset: ")+e.animatorController,{.55f,.78f,1});
            else ui.label("Assign or drag an Animator Controller here.",{.55f,.59f,.66f});
            ui.label("Traces look up this object or one of its children by name.",{.55f,.65f,.75f});
            ui.label("Animated names must be unique within the hierarchy.",{.55f,.59f,.66f});
            if(g.mode==Mode::Play&&e.animatorRuntimeState){
                AnimatorControllerAsset* runtime=runtimeAnimatorController(e.animatorController);
                const AnimatorState* state=runtime?runtime->byId(e.animatorRuntimeState):nullptr;
                char status[128];snprintf(status,sizeof(status),"State: %s | %.2f s",state?state->name.c_str():"?",e.animatorRuntimeTime);
                ui.label(status,{.45f,.95f,.62f});
            }
        }
        ui.componentEnd();
    }

    if(detailKind==DETAIL_INSPECTOR_EVENTS&&e.hasInspectorEvents){
        bool collapsed=(e.detailCollapsed&(1u<<DETAIL_INSPECTOR_EVENTS))!=0;
        int card=optionalHeader(DETAIL_INSPECTOR_EVENTS,"detail_inspector_events","INSPECTOR EVENTS",true,collapsed);
        if(card&UI::COMP_REMOVE){e.hasInspectorEvents=false;e.inspectorEvents.clear();addLog(0,"Inspector Events removed from %s.",e.name);}
        else if(!collapsed){
            ui.label("Persistent events: each event can call several listeners.",{.55f,.65f,.75f});
            int removeEvent=-1;
            for(int eventIndex=0;eventIndex<(int)e.inspectorEvents.size();eventIndex++){
                InspectorEventDef& event=e.inspectorEvents[eventIndex];
                char eventName[64];snprintf(eventName,sizeof(eventName),"%s",event.name.c_str());
                char eventId[64];snprintf(eventId,sizeof(eventId),"Nome evento##inspector_event_%d",eventIndex);
                if(ui.textInput(eventId,eventName,sizeof(eventName))){
                    std::string candidate=eventName;for(char& c:candidate)if(c==' ')c='_';
                    if(candidate.empty())candidate="Event";
                    bool duplicate=false;for(int other=0;other<(int)e.inspectorEvents.size();other++)
                        if(other!=eventIndex&&_stricmp(e.inspectorEvents[other].name.c_str(),candidate.c_str())==0)duplicate=true;
                    if(!duplicate)event.name=candidate;
                }
                int removeListener=-1;
                for(int listenerIndex=0;listenerIndex<(int)event.listeners.size();listenerIndex++){
                    InspectorEventListener& listener=event.listeners[listenerIndex];
                    Entity* target=g.scene.byId(listener.targetEntity);
                    char targetLabel[128];snprintf(targetLabel,sizeof(targetLabel),"%d. Target: %s  [drag from the Outliner]",listenerIndex+1,target?target->name:"None");
                    float y0=ui.panelCursorY();UIRect panel=ui.panelInner();bool clearTarget=ui.button(targetLabel);float y1=ui.panelCursorY();
                    VarDropTarget drop{};drop.x=panel.x;drop.y=y0;drop.w=panel.w;drop.h=(std::max)(22.0f,y1-y0);drop.entId=e.id;
                    drop.inspectorEvent=eventIndex;drop.inspectorListener=listenerIndex;g_varDrops.push_back(drop);
                    if(clearTarget&&listener.targetEntity){listener.targetEntity=0;listener.callable.clear();listener.arguments.clear();target=nullptr;}

                    std::vector<std::pair<std::string,BPGraph*>> targetGraphs;
                    if(target)for(int blueprintIndex=0;blueprintIndex<entityBlueprintCount(*target);blueprintIndex++){
                        const char* blueprintPath=entityBlueprintPath(*target,blueprintIndex);BPGraph* graph=editGraph(blueprintPath);
                        if(graph)targetGraphs.push_back({fs::path(blueprintPath).stem().string(),graph});
                    }
                    std::vector<std::string> callableNames{"None"},callableKeys{""};std::vector<bool> callableEvents{false};
                    std::vector<std::vector<BPFuncPin>> callableSignatures(1);
                    auto nativePin=[](const char* name,PinKind kind){BPFuncPin pin;snprintf(pin.name,sizeof(pin.name),"%s",name);pin.kind=kind;return pin;};
                    auto addNative=[&](const char* label,const char* key,std::vector<BPFuncPin> signature={}){
                        callableNames.push_back(label);callableKeys.push_back(key);callableEvents.push_back(false);callableSignatures.push_back(std::move(signature));};
                    if(target){
                        addNative("Transform / Set World Location","@Transform.SetWorldLocation",{nativePin("Location",PIN_VEC)});
                        addNative("Transform / Set World Rotation","@Transform.SetWorldRotation",{nativePin("Rotation",PIN_VEC)});
                        if(target->hasMesh)addNative("Mesh Renderer / Set Color","@MeshRenderer.SetColor",{nativePin("Color",PIN_COLOR)});
                        if(target->hasAudio){addNative("Audio Source / Play","@AudioSource.Play");addNative("Audio Source / Stop","@AudioSource.Stop");addNative("Audio Source / Set Volume","@AudioSource.SetVolume",{nativePin("Volume",PIN_NUM)});}
                        if(target->hasAIAgent)addNative("AI Agent / Set Stopped","@AIAgent.SetStopped",{nativePin("Stopped",PIN_BOOL)});
                    }
                    for(auto& blueprint:targetGraphs){BPGraph* targetGraph=blueprint.second;
                        for(BPFunc& function:targetGraph->funcs){callableNames.push_back(blueprint.first+" / "+function.name);callableKeys.push_back(function.name);callableEvents.push_back(false);callableSignatures.push_back(function.ins);}
                        for(BPEventDef& custom:targetGraph->events){callableNames.push_back(blueprint.first+" / Custom Event / "+custom.name);callableKeys.push_back(custom.name);callableEvents.push_back(true);callableSignatures.push_back(custom.params);}
                    }
                    std::vector<const char*> callableLabels;for(std::string& name:callableNames)callableLabels.push_back(name.c_str());
                    int selectedCallable=0;
                    for(int option=1;option<(int)callableNames.size();option++)
                        if(listener.customEvent==callableEvents[option]&&listener.callable==callableKeys[option])selectedCallable=option;
                    char callableId[64];snprintf(callableId,sizeof(callableId),"Funzione / evento##inspector_callable_%d_%d",eventIndex,listenerIndex);
                    if(ui.combo(callableId,&selectedCallable,callableLabels.data(),(int)callableLabels.size())){
                        listener.callable.clear();listener.arguments.clear();
                        if(selectedCallable>0){
                            listener.customEvent=callableEvents[selectedCallable];
                            listener.callable=callableKeys[selectedCallable];
                            for(const BPFuncPin& pin:callableSignatures[selectedCallable]){InspectorEventArgument argument;argument.kind=(int)pin.kind;listener.arguments.push_back(argument);}
                        }
                    }
                    const std::vector<BPFuncPin>* signature=nullptr;
                    if(selectedCallable>0&&selectedCallable<(int)callableSignatures.size())signature=&callableSignatures[selectedCallable];
                    if(signature&&listener.arguments.size()!=signature->size()){
                        listener.arguments.resize(signature->size());for(size_t i=0;i<signature->size();i++)listener.arguments[i].kind=(int)(*signature)[i].kind;
                    }
                    if(signature)for(int argumentIndex=0;argumentIndex<(int)signature->size();argumentIndex++){
                        const BPFuncPin& pin=(*signature)[argumentIndex];InspectorEventArgument& argument=listener.arguments[argumentIndex];argument.kind=(int)pin.kind;
                        char inputId[96];snprintf(inputId,sizeof(inputId),"%s##inspector_arg_%d_%d_%d",pin.name,eventIndex,listenerIndex,argumentIndex);
                        if(pin.kind==PIN_BOOL){bool value=argument.value.x!=0;if(ui.checkbox(inputId,&value))argument.value.x=value?1.0f:0.0f;}
                        else if(pin.kind==PIN_INT){int value=(int)argument.value.x;if(ui.dragInt(inputId,&value,.1f,-100000,100000))argument.value.x=(float)value;}
                        else if(pin.kind==PIN_VEC||pin.kind==PIN_VEC2){ui.label(pin.name,{.65f,.72f,.82f});ui.row(pin.kind==PIN_VEC?3:2);
                            char axis[96];snprintf(axis,sizeof(axis),"X##%s",inputId);ui.dragFloat(axis,&argument.value.x,.05f,-100000,100000);
                            snprintf(axis,sizeof(axis),"Y##%s",inputId);ui.dragFloat(axis,&argument.value.y,.05f,-100000,100000);
                            if(pin.kind==PIN_VEC){snprintf(axis,sizeof(axis),"Z##%s",inputId);ui.dragFloat(axis,&argument.value.z,.05f,-100000,100000);}}
                        else if(pin.kind==PIN_STR){char textValue[128];snprintf(textValue,sizeof(textValue),"%s",argument.text.c_str());if(ui.textInput(inputId,textValue,sizeof(textValue)))argument.text=textValue;}
                        else if(pin.kind==PIN_COLOR)ui.colorEditRGBA(inputId,&argument.value,&argument.alpha);
                        else if(pin.kind==PIN_ENT||pin.kind==PIN_TRANSFORM){std::vector<const char*> names{"None"};std::vector<int> ids{0};int selected=0;
                            for(Entity& object:g.scene.entities){ids.push_back(object.id);names.push_back(object.name);if(object.id==argument.objectId)selected=(int)ids.size()-1;}
                            if(ui.combo(inputId,&selected,names.data(),(int)names.size()))argument.objectId=ids[selected];}
                        else ui.dragFloat(inputId,&argument.value.x,.05f,-100000,100000);
                    }
                    char removeId[64];snprintf(removeId,sizeof(removeId),"- Remove listener##%d_%d",eventIndex,listenerIndex);
                    if(ui.buttonColored(removeId,{.36f,.12f,.14f},{1,.82f,.82f}))removeListener=listenerIndex;
                    ui.separator();
                }
                if(removeListener>=0)event.listeners.erase(event.listeners.begin()+removeListener);
                char addListenerId[64];snprintf(addListenerId,sizeof(addListenerId),"+ Listener##%d",eventIndex);
                if(ui.button(addListenerId))event.listeners.push_back({});
                char removeEventId[64];snprintf(removeEventId,sizeof(removeEventId),"Remove event##%d",eventIndex);
                if(ui.buttonColored(removeEventId,{.40f,.12f,.14f},{1,.82f,.82f}))removeEvent=eventIndex;
                ui.spacing(5);
            }
            if(removeEvent>=0)e.inspectorEvents.erase(e.inspectorEvents.begin()+removeEvent);
            if(ui.button("+ Inspector Event")){
                InspectorEventDef event;int suffix=(int)e.inspectorEvents.size()+1;event.name="Event"+std::to_string(suffix);
                while(std::any_of(e.inspectorEvents.begin(),e.inspectorEvents.end(),[&](const InspectorEventDef& existing){return existing.name==event.name;}))event.name="Event"+std::to_string(++suffix);
                e.inspectorEvents.push_back(std::move(event));
            }
            ui.label("Call the list from the Blueprint with Invoke Inspector Event.",{.45f,.78f,.95f});
        }
        ui.componentEnd();
    }

    if (detailKind == DETAIL_SIMPLE_SCRIPT && e.behavior != BH_NONE) {
        bool collapsed = (e.detailCollapsed & (1u << DETAIL_SIMPLE_SCRIPT)) != 0;
        int card = optionalHeader(DETAIL_SIMPLE_SCRIPT, "detail_simple_script", "SCRIPT SEMPLICE", true, collapsed);
        if (card & UI::COMP_REMOVE) {
            e.behavior = BH_NONE;
            addLog(0, "Simple script removed from %s.", e.name);
        } else if (!collapsed) {
            ui.combo("Comportamento", &e.behavior, BEHAVIOR_NAMES, BH_COUNT);
            if (e.behavior == BH_JUMP_SPACE) {
                ui.dragFloat("Jump speed", &e.bp[0], 0.05f, 0, 40);
            } else if (e.behavior == BH_SPIN) {
                ui.dragFloat("Y torque", &e.bp[0], 0.1f, -100, 100);
            } else if (e.behavior == BH_PUSH_START) {
                ui.label("Initial velocity", { 0.55f, 0.59f, 0.66f });
                ui.row(3);
                ui.dragFloat("#bx", &e.bp[0], 0.1f, -60, 60);
                ui.dragFloat("#by", &e.bp[1], 0.1f, -60, 60);
                ui.dragFloat("#bz", &e.bp[2], 0.1f, -60, 60);
            }
        }
        ui.componentEnd();
    }

    if (detailKind == DETAIL_BLUEPRINT && entityBlueprintCount(e) > 0) {
        int removeComponent = -1;
        int componentCount = entityBlueprintCount(e);
        for (int componentIndex = 0; componentIndex < componentCount; componentIndex++) {
            const std::string path = entityBlueprintPath(e, componentIndex);
            std::map<std::string, Vec3>* valueMap = entityBlueprintOverrides(e, componentIndex);
            std::map<std::string, float>* alphaMap = entityBlueprintAlphaOverrides(e, componentIndex);
            if (path.empty() || !valueMap || !alphaMap) continue;
            bool collapsed = componentIndex == 0
                           ? (e.detailCollapsed & (1u << DETAIL_BLUEPRINT)) != 0
                           : e.additionalBlueprints[componentIndex - 1].collapsed;
            std::string scopeName = "blueprint_component_" + std::to_string(componentIndex);
            uint32_t previousScope = ui.pushId(scopeName.c_str());
            ui.spacing(6);
            std::string title = fs::path(path).stem().string();
            if (title.empty()) title = "Blueprint";
            // Blueprint cards drag exactly like the built-in components: within the
            // blueprint list, and (as a block) among the other component cards.
            bool bpDragging = g.detailsDragEntity == e.id && g.detailsDragComponent == DETAIL_BLUEPRINT &&
                              g.detailsDragBpIndex == componentIndex && ui.input().mouseDown;
            int card = ui.componentBegin("card", title.c_str(), collapsed, true, bpDragging, false, true);
            if (card & UI::COMP_TOGGLED) {
                collapsed = !collapsed;
                if (componentIndex == 0) e.detailCollapsed ^= 1u << DETAIL_BLUEPRINT;
                else e.additionalBlueprints[componentIndex - 1].collapsed = collapsed;
            }
            if (card & UI::COMP_RESET) openComponentResetMenu(ui, e, DETAIL_BLUEPRINT);
            beginCardDrag(DETAIL_BLUEPRINT, componentIndex, card, title.c_str());
            if (card & UI::COMP_REMOVE) {
                removeComponent = componentIndex;
            } else if (!collapsed) {
                ui.label(std::string("Asset: ") + path, { 0.75f, 0.85f, 1.0f });
                if (ui.button("Open in the Blueprint editor")) openBlueprintDoc(g.projectDir + "\\" + path, path);
                BPGraph* bg = editGraph(path.c_str());
                if (bg) {
                    bool any = false;
                    for (const auto& v : bg->vars) {
                        if (!v.expose || v.container != VC_SINGLE) continue;
                        if (!any) { ui.label("Exposed variables:", { 0.55f, 0.59f, 0.66f }); any = true; }
                        Vec3 cur = valueMap->count(v.name) ? (*valueMap)[v.name] : v.def;
                        char idb[64];
                        if (v.type == PIN_VEC || v.type == PIN_VEC2) {
                            ui.label(v.name, { 0.72f, 0.95f, 0.75f });
                            ui.row(v.type == PIN_VEC ? 3 : 2);
                            bool changed = false;
                            snprintf(idb, sizeof(idb), "#vx##%s", v.name); changed |= ui.dragFloat(idb, &cur.x, .05f, -10000, 10000);
                            snprintf(idb, sizeof(idb), "#vy##%s", v.name); changed |= ui.dragFloat(idb, &cur.y, .05f, -10000, 10000);
                            if (v.type == PIN_VEC) { snprintf(idb, sizeof(idb), "#vz##%s", v.name); changed |= ui.dragFloat(idb, &cur.z, .05f, -10000, 10000); }
                            if (changed) (*valueMap)[v.name] = cur;
                        } else if (v.type == PIN_COLOR) {
                            // the colour picker keeps a live pointer across frames, so it must
                            // address the persistent override map, not a per-frame local copy
                            if (!valueMap->count(v.name)) (*valueMap)[v.name] = v.def;
                            if (!alphaMap->count(v.name)) (*alphaMap)[v.name] = v.defAlpha;
                            ui.colorEditRGBA(v.name, &(*valueMap)[v.name], &(*alphaMap)[v.name]);
                        } else if (v.type == PIN_INT) {
                            int value = (int)cur.x; snprintf(idb, sizeof(idb), "%s##iv", v.name);
                            if (ui.dragInt(idb, &value, .1f, -100000, 100000)) (*valueMap)[v.name] = { (float)value, 0, 0 };
                        } else if (v.type == PIN_ENUM) {
                            BPEnumAsset en; std::string enumData;
                            if (v.enumAsset[0] && readFile(g.projectDir + "\\" + v.enumAsset, enumData) && en.deserialize(enumData)) {
                                int value = (int)cur.x; if (value < 0 || value >= (int)en.values.size()) value = 0;
                                std::vector<const char*> names; for (const std::string& item : en.values) names.push_back(item.c_str());
                                snprintf(idb, sizeof(idb), "%s##enum", v.name);
                                if (ui.combo(idb, &value, names.data(), (int)names.size())) (*valueMap)[v.name] = { (float)value, 0, 0 };
                            } else ui.label(std::string(v.name) + ": missing Enum", { .95f, .58f, .28f });
                        } else if (v.type == PIN_STR) {
                            ui.label(std::string(v.name) + ": " + v.strDef, { .72f, .95f, .75f });
                        } else if (v.type == PIN_ANIMATION_CLIP || v.type == PIN_ANIMATOR_CONTROLLER ||
                                   (v.type == PIN_ENT && strcmp(v.refClass, "asset:AnimatorController") == 0)) {
                            const char* kind = v.type == PIN_ANIMATION_CLIP ? "Animation Clip" : "Animator Controller (Object)";
                            ui.label(std::string(v.name) + " (" + kind + "): " + (v.assetPath[0] ? v.assetPath : "None"),
                                     v.assetPath[0] ? Vec3{ .72f, .84f, 1.0f } : Vec3{ .55f, .59f, .66f });
                        } else if (v.type == PIN_BOOL) {
                            bool value = cur.x != 0; snprintf(idb, sizeof(idb), "%s##bv", v.name);
                            if (ui.checkbox(idb, &value)) (*valueMap)[v.name] = { value ? 1.0f : 0.0f, 0, 0 };
                        } else if (v.type == PIN_TIMER_HANDLE) {
                            ui.label(std::string(v.name) + " (Timer Handle): assigned during Play", { .38f, .88f, .92f });
                        } else if (v.type == PIN_TRANSFORM) {
                            // explicit transform value: Location / Rotation / Scale (rot & scale
                            // are stored under mangled override keys "<name>#rot" / "<name>#scl")
                            ui.label(std::string(v.name) + " (Transform)", { 0.72f, 0.95f, 0.75f });
                            std::string rotKey = std::string(v.name) + "#rot", sclKey = std::string(v.name) + "#scl";
                            Vec3 loc = valueMap->count(v.name) ? (*valueMap)[v.name] : v.def;
                            Vec3 rot = valueMap->count(rotKey) ? (*valueMap)[rotKey] : v.defRot;
                            Vec3 scl = valueMap->count(sclKey) ? (*valueMap)[sclKey] : v.defScl;
                            bool cl = false, cr = false, cs = false;
                            ui.label("Location", { 0.6f, 0.64f, 0.7f }); ui.row(3);
                            snprintf(idb, sizeof(idb), "#tlx##%s", v.name); cl |= ui.dragFloat(idb, &loc.x, .05f, -100000, 100000);
                            snprintf(idb, sizeof(idb), "#tly##%s", v.name); cl |= ui.dragFloat(idb, &loc.y, .05f, -100000, 100000);
                            snprintf(idb, sizeof(idb), "#tlz##%s", v.name); cl |= ui.dragFloat(idb, &loc.z, .05f, -100000, 100000);
                            ui.label("Rotation", { 0.6f, 0.64f, 0.7f }); ui.row(3);
                            snprintf(idb, sizeof(idb), "#trx##%s", v.name); cr |= ui.dragFloat(idb, &rot.x, .1f, -100000, 100000);
                            snprintf(idb, sizeof(idb), "#try##%s", v.name); cr |= ui.dragFloat(idb, &rot.y, .1f, -100000, 100000);
                            snprintf(idb, sizeof(idb), "#trz##%s", v.name); cr |= ui.dragFloat(idb, &rot.z, .1f, -100000, 100000);
                            ui.label("Scale", { 0.6f, 0.64f, 0.7f }); ui.row(3);
                            snprintf(idb, sizeof(idb), "#tsx##%s", v.name); cs |= ui.dragFloat(idb, &scl.x, .02f, -100000, 100000);
                            snprintf(idb, sizeof(idb), "#tsy##%s", v.name); cs |= ui.dragFloat(idb, &scl.y, .02f, -100000, 100000);
                            snprintf(idb, sizeof(idb), "#tsz##%s", v.name); cs |= ui.dragFloat(idb, &scl.z, .02f, -100000, 100000);
                            if (cl) (*valueMap)[v.name] = loc;
                            if (cr) (*valueMap)[rotKey] = rot;
                            if (cs) (*valueMap)[sclKey] = scl;
                        } else if (v.type == PIN_ENT) {
                            int refId = (int)cur.x; Entity* referenced = refId > 0 ? g.scene.byId(refId) : nullptr;
                            std::string typeName = refClassLabel(v.refClass);
                            char label[110]; snprintf(label, sizeof(label), "%s (%s): %s", v.name, typeName.c_str(), referenced ? referenced->name : "None  [drag here]");
                            float y0 = ui.panelCursorY(); UIRect panel = ui.panelInner(); bool clear = ui.button(label); float y1 = ui.panelCursorY();
                            VarDropTarget target{}; target.x = panel.x; target.y = y0; target.w = panel.w; target.h = y1 > y0 ? y1 - y0 : 22;
                            target.entId = e.id; target.blueprintComponent = componentIndex; target.type = v.type;
                            snprintf(target.var, sizeof(target.var), "%s", v.name); snprintf(target.refClass, sizeof(target.refClass), "%s", v.refClass);
                            g_varDrops.push_back(target);
                            if (clear && refId > 0) valueMap->erase(v.name);
                        } else {
                            snprintf(idb, sizeof(idb), "%s##nv", v.name);
                            if (ui.dragFloat(idb, &cur.x, .05f, -100000, 100000)) (*valueMap)[v.name] = cur;
                        }
                    }
                }
            }
            ui.componentEnd();
            ui.popId(previousScope);
        }
        if (removeComponent >= 0) {
            std::string removedName = fs::path(entityBlueprintPath(e, removeComponent)).stem().string();
            removeBlueprintComponent(e, removeComponent);
            addLog(0, "Component %s removed from %s.", removedName.c_str(), e.name);
        }
    }

    // joints touching this entity (constraint component)
    if (detailKind == DETAIL_JOINTS) {
        std::vector<int> myJoints;
        for (int ji = 0; ji < (int)g.scene.joints.size(); ji++) {
            if (g.scene.joints[ji].entA == e.id || g.scene.joints[ji].entB == e.id) myJoints.push_back(ji);
        }
        if (!myJoints.empty()) {
        bool collapsed = (e.detailCollapsed & (1u << DETAIL_JOINTS)) != 0;
        optionalHeader(DETAIL_JOINTS, "detail_joints", "CONSTRAINTS", false, collapsed);
        int removeJi = -1;
        if (!collapsed) {
        for (int k = 0; k < (int)myJoints.size(); k++) {
            JointDef& j = g.scene.joints[myJoints[k]];
            ui.spacing(4);
            char hd[64];
            snprintf(hd, sizeof(hd), "CONSTRAINT %d", k + 1);
            if (ui.headerClosable(hd)) {
                removeJi = myJoints[k];
            } else {
                int otherId = j.entA == e.id ? j.entB : j.entA;
                Entity* other = g.scene.byId(otherId);
                ui.label(std::string("Collegato a: ") + (other ? other->name : "?"), { 0.75f, 0.85f, 1.0f });
                static const char* JMODES[] = { "Rigid rod", "Rope (pull only)" };
                int jm = j.rope ? 1 : 0;
                char idb[48];
                snprintf(idb, sizeof(idb), "Modo##j%d", k);
                if (ui.combo(idb, &jm, JMODES, 2)) {
                    j.rope = jm == 1;
                    g.scene.rebuildConstraints();
                }
                snprintf(idb, sizeof(idb), "Lunghezza##j%d", k);
                if (ui.dragFloat(idb, &j.len, 0.02f, 0.05f, 200)) g.scene.rebuildConstraints();
                snprintf(idb, sizeof(idb), "Break (0=never)##j%d", k);
                if (ui.dragFloat(idb, &j.breakImp, 0.1f, 0, 10000)) g.scene.rebuildConstraints();
            }
        }
        }
        if (removeJi >= 0) {
            g.scene.joints.erase(g.scene.joints.begin() + removeJi);
            g.scene.rebuildConstraints();
            addLog(0, "Constraint removed.");
        }
        ui.componentEnd();
        }
    }
    }

    // Physics Constraint card: rendered after the reorderable component loop
    if (e.hasConstraint) {
        bool collapsed = (e.detailCollapsed & (1u << DETAIL_CONSTRAINT)) != 0;
        int card = optionalHeader(DETAIL_CONSTRAINT, "detail_constraint", "PHYSICS CONSTRAINT", true, collapsed);
        if (card & UI::COMP_REMOVE) {
            e.hasConstraint = false;
            g.scene.rebuildConstraints();
            addLog(0, "Physics Constraint removed from %s.", e.name);
        } else if (!collapsed) {
            const UIInput& in = ui.input();
            Renderer* r = ui.r;
            // two object references: dropdown picker + drag-drop from the Outliner
            std::vector<const char*> names; std::vector<int> ids;
            names.push_back("(none)"); ids.push_back(0);
            for (Entity& o : g.scene.entities) { if (o.id == e.id) continue; names.push_back(o.name); ids.push_back(o.id); }
            auto refField = [&](const char* label, int* ref, int slot) {
                int sel = 0;
                for (int i = 0; i < (int)ids.size(); i++) if (ids[i] == *ref) sel = i;
                if (ui.combo(label, &sel, names.data(), (int)names.size())) { *ref = ids[sel]; g.scene.rebuildConstraints(); }
                UIRect rc = ui.lastItemRect();
                VarDropTarget t{}; t.x = rc.x; t.y = rc.y; t.w = rc.w; t.h = rc.h; t.entId = e.id; t.constraintSlot = slot;
                g_varDrops.push_back(t);
                if (g.treeDragging && in.mouseX >= rc.x && in.mouseX < rc.x + rc.w && in.mouseY >= rc.y && in.mouseY < rc.y + rc.h)
                    r->drawRectPx(rc.x, rc.y, rc.w, rc.h, { 0.16f, 0.34f, 0.52f }, 0.5f);   // drop highlight
            };
            refField("Oggetto 1 (verde)", &e.constraintObjA, 1);
            refField("Oggetto 2 (rosso)", &e.constraintObjB, 2);
            ui.label("Drag objects from the Outliner onto the fields to link them.", { .55f, .59f, .66f });

            static const char* LIMMODE[] = { "Free", "Limited", "Bloccato" };
            const char* AX[3] = { "X", "Y", "Z" };
            ui.header("LINEAR LIMITS");
            for (int a = 0; a < 3; a++) {
                char idb[56];
                snprintf(idb, sizeof(idb), "%s##conlm%d", AX[a], a);
                if (ui.combo(idb, &e.conLinMode[a], LIMMODE, 3)) g.scene.rebuildConstraints();
                if (e.conLinMode[a] == 1) { snprintf(idb, sizeof(idb), "Limite %s (m)##conll%d", AX[a], a);
                    ui.dragFloat(idb, &e.conLinLimit[a], 0.01f, 0, 100); }
            }
            ui.header("ANGULAR LIMITS");
            for (int a = 0; a < 3; a++) {
                char idb[56];
                snprintf(idb, sizeof(idb), "%s##conam%d", AX[a], a);
                if (ui.combo(idb, &e.conAngMode[a], LIMMODE, 3)) g.scene.rebuildConstraints();
                if (e.conAngMode[a] == 1) { snprintf(idb, sizeof(idb), "Limite %s (deg)##conal%d", AX[a], a);
                    ui.dragFloat(idb, &e.conAngLimit[a], 0.5f, 0, 180); }
            }
            ui.header("MOTORS");
            ui.checkbox("Linear motor", &e.conLinMotor);
            if (e.conLinMotor) {
                ui.label("Target speed", { .65f, .72f, .82f }); ui.row(3);
                ui.dragFloat("X##clmvx", &e.conLinMotorTarget.x, .02f, -100, 100);
                ui.dragFloat("Y##clmvy", &e.conLinMotorTarget.y, .02f, -100, 100);
                ui.dragFloat("Z##clmvz", &e.conLinMotorTarget.z, .02f, -100, 100);
                ui.dragFloat("Forza max##clmf", &e.conLinMotorForce, 1, 0, 100000);
            }
            ui.checkbox("Motore angolare", &e.conAngMotor);
            if (e.conAngMotor) {
                ui.label("Target speed (deg/s)", { .65f, .72f, .82f }); ui.row(3);
                ui.dragFloat("X##camvx", &e.conAngMotorTarget.x, .5f, -1000, 1000);
                ui.dragFloat("Y##camvy", &e.conAngMotorTarget.y, .5f, -1000, 1000);
                ui.dragFloat("Z##camvz", &e.conAngMotorTarget.z, .5f, -1000, 1000);
                ui.dragFloat("Forza max##camf", &e.conAngMotorForce, 1, 0, 100000);
            }
            ui.header("ROTTURA");
            if (ui.dragFloat("Break impulse (0=never)", &e.conBreak, 0.1f, 0, 100000)) g.scene.rebuildConstraints();
        }
        ui.componentEnd();
    }

    // ── smooth card drag (Unity-style): the grabbed section follows the cursor
    // inside the Details panel and the drop snaps to the gap between two cards ──
    if (g.detailsDragEntity == e.id && g.detailsDragComponent >= DETAIL_MESH && !g.detailCards.empty()) {
        const UIInput& din = ui.input();
        Renderer* r = ui.r;
        UIRect clip = ui.panelClip();
        const auto& cards = g.detailCards;
        // insertion slot = how many card midpoints the cursor has passed
        int slot = 0;
        for (const auto& c : cards) if (din.mouseY > c.y + c.h * 0.5f) slot++;
        int dragSlot = -1;
        for (int i = 0; i < (int)cards.size(); i++)
            if (cards[i].kind == g.detailsDragComponent && cards[i].bpIndex == g.detailsDragBpIndex) dragSlot = i;

        if (din.mouseDown && dragSlot >= 0) {
            float lineY = slot <= 0 ? cards.front().y - 4
                        : slot >= (int)cards.size() ? cards.back().y + cards.back().h + 4
                        : (cards[slot - 1].y + cards[slot - 1].h + cards[slot].y) * 0.5f;
            float lx = clip.x + 6, lw = clip.w - 12;
            // ghost of the grabbed section, kept inside the Details panel
            float gh = g.detailsDragCardH;
            float gy = clampf(din.mouseY - g.detailsDragGrabDY, clip.y, clip.y + clip.h - gh);
            r->drawRectPx(lx + 3, gy + 3, lw, gh, { 0, 0, 0 }, 0.30f);
            r->drawRectPx(lx, gy, lw, gh, { 0.16f, 0.34f, 0.52f }, 0.80f);
            r->drawRectPx(lx, gy, 3, gh, { 0.35f, 0.75f, 1.0f }, 1);
            r->drawTextLine(lx + 10, gy + 4, ui.ellipsize(g.detailsDragTitle, lw - 20), { 0.93f, 0.96f, 1.0f }, 1);
            // snapped insertion marker — drawn last so it stays readable even when
            // the ghost sits right on top of the drop gap
            r->drawRectPx(lx - 4, lineY - 1.5f, lw + 8, 3, { 0.45f, 0.85f, 1.0f }, 1);
            r->drawRectPx(lx - 4, lineY - 6, 4, 12, { 0.45f, 0.85f, 1.0f }, 1);
            r->drawRectPx(lx + lw, lineY - 6, 4, 12, { 0.45f, 0.85f, 1.0f }, 1);
        } else if (din.mouseReleased && dragSlot >= 0 && slot != dragSlot && slot != dragSlot + 1) {
            const auto& src = cards[dragSlot];
            // Dropping between two blueprint cards reorders the blueprint list;
            // anywhere else moves the whole component kind in detailOrder.
            bool beforeIsBp = slot > 0 && cards[slot - 1].bpIndex >= 0;
            bool afterIsBp = slot < (int)cards.size() && cards[slot].bpIndex >= 0;
            if (src.bpIndex >= 0 && (beforeIsBp || afterIsBp)) {
                int target = afterIsBp ? cards[slot].bpIndex : cards[slot - 1].bpIndex + 1;
                moveEntityBlueprint(e, src.bpIndex, target);
            } else {
                auto& order = e.detailOrder;
                auto from = std::find(order.begin(), order.end(), src.kind);
                if (from != order.end()) {
                    order.erase(from);
                    // insert before the kind that will follow the drop point
                    int anchorKind = -1;
                    for (int i = slot; i < (int)cards.size(); i++)
                        if (cards[i].kind != src.kind) { anchorKind = cards[i].kind; break; }
                    auto at = anchorKind >= 0 ? std::find(order.begin(), order.end(), anchorKind) : order.end();
                    order.insert(at, src.kind);
                }
            }
        }
    }
    if (ui.input().mouseReleased && g.detailsDragEntity == e.id) {
        g.detailsDragEntity = 0;
        g.detailsDragComponent = -1;
        g.detailsDragBpIndex = -1;
    }

#if 0 // Object-level actions moved to the Outliner context menu.
    // ── add component ──
    ui.spacing(6);
    if (ui.buttonColored(g.addCompOpen ? "- Add component" : "+ Add component",
                         g.addCompOpen ? Vec3{ 0.12f, 0.32f, 0.56f } : Vec3{ 0.16f, 0.18f, 0.22f },
                         g.addCompOpen ? Vec3{ 0.8f, 0.92f, 1.0f } : Vec3{ 0.85f, 0.88f, 0.93f }))
        g.addCompOpen = !g.addCompOpen;
    if (g.addCompOpen) {
        if (!e.hasMesh && ui.button("  Mesh Renderer")) {
            e.hasMesh = true;
            g.scene.syncBodyShape(e);
            g.addCompOpen = false;
        }
        if (!e.hasPhysics && ui.button("  Physics (rigid body)")) {
            e.hasPhysics = true;
            g.scene.syncBodyShape(e);
            g.addCompOpen = false;
        }
        if (!e.hasTrigger && ui.button("  Trigger")) {
            e.hasTrigger = true;
            e.collision = 1;
            g.scene.syncBodyShape(e);
            g.addCompOpen = false;
            addLog(1, "Trigger added to %s: choose Box, Sphere or Capsule.", e.name);
        }
        if (!e.isLight && ui.button("  Luce puntuale")) {
            e.isLight = true;
            g.addCompOpen = false;
        }
        if (!e.isCamera && ui.button("  Camera")) {
            e.isCamera = true;
            g.addCompOpen = false;
            addLog(1, "Camera added to %s: press Play to look through it.", e.name);
        }
        if (!e.hasAudio && ui.button("  Audio Source")) {
            e.hasAudio = true;
            g.addCompOpen = false;
            addLog(1, "Audio Source added to %s: drag an audio clip into the Details.", e.name);
        }
        if (!e.hasReverb && ui.button("  Audio Reverb Zone")) {
            e.hasReverb = true;
            g.addCompOpen = false;
            addLog(1, "Audio Reverb Zone added to %s.", e.name);
        }
        if (!e.hasAIAgent && ui.button("  AI Agent")) {
            e.hasAIAgent = true;
            e.aiDestination = e.body->position;
            g.addCompOpen = false;
            addLog(1, "AI Agent added to %s: bake the NavMesh from the Navigation tab.", e.name);
        }
        if (!e.hasNavigationOccluder && ui.button("  Navigation Occluder")) {
            e.hasNavigationOccluder = true;
            g.addCompOpen = false;
            invalidateNavigation();
            addLog(1, "Navigation Occluder added to %s: it will be subtracted on the next Bake.", e.name);
        }
        if (!e.hasAnimator && ui.button("  Animator")) {
            e.hasAnimator=true;e.animatorController[0]=0;e.animatorPlayOnAwake=true;e.animatorSpeed=1;
            g.addCompOpen=false;
            addLog(1,"Animator added to %s: assign an Animator Controller.",e.name);
        }
        if(!e.hasInspectorEvents&&ui.button("  Inspector Events")){
            e.hasInspectorEvents=true;InspectorEventDef event;event.name="Event1";e.inspectorEvents.push_back(std::move(event));
            g.addCompOpen=false;addLog(1,"Inspector Events added to %s.",e.name);
        }
        if (e.behavior == BH_NONE && ui.button("  Simple script")) {
            e.behavior = BH_JUMP_SPACE;
            g.addCompOpen = false;
        }
        bool hasOpenBlueprint = false;
        std::unordered_set<std::string> listedBlueprints;
        for (int documentIndex = 0; documentIndex < (int)g.bpDocs.size(); documentIndex++) {
            BPEditor* document = g.bpDocs[documentIndex].get();
            if (!document || document->curPath.empty() || !listedBlueprints.insert(document->curPath).second) continue;
            hasOpenBlueprint = true;
            std::string componentName = fs::path(document->curPath).stem().string();
            std::string buttonLabel = "  " + componentName + "##add_blueprint_" + std::to_string(documentIndex);
            if (ui.button(buttonLabel.c_str())) {
                bool added = addBlueprintComponent(e, document->curPath);
                addLog(added ? 1 : 2, added ? "Component %s added to %s."
                                            : "Component %s is single-instance and is already on %s.",
                       componentName.c_str(), e.name);
                g.addCompOpen = false;
            }
        }
        if (!hasOpenBlueprint)
            ui.label("Open and save a Blueprint to add it as a component.", { .55f, .59f, .66f });
        // constraint toward another entity
        {
            std::vector<const char*> names;
            std::vector<int> ids;
            for (auto& o : g.scene.entities) {
                if (o.id == e.id) continue;
                names.push_back(o.name);
                ids.push_back(o.id);
            }
            if (!names.empty()) {
                if (g.jointTargetPick >= (int)names.size()) g.jointTargetPick = 0;
                ui.combo("Constraint to", &g.jointTargetPick, names.data(), (int)names.size());
                if (ui.button("  Create constraint (rod)")) {
                    g.scene.addJoint(e.id, ids[g.jointTargetPick], -1, false);
                    addLog(1, "Constraint created to %s.", names[g.jointTargetPick]);
                    g.addCompOpen = false;
                }
            }
        }
    }

    ui.spacing(6);
    ui.row(3);
    bool entityDeleted = false;
    if (ui.button("Duplicate")) {
        std::vector<int> ids = g.scene.duplicateSubtree(e.id);
        for (int id : ids) {
            Entity* ne = g.scene.byId(id);
            if (ne && (ne->parentId == 0 || !g.scene.byId(ne->parentId))) { g.selectedId = id; break; }
        }
        addLog(0, "Duplicated (%d objects).", (int)ids.size());
    }
    if (ui.button("Prefab...")) savePrefab();
    if (ui.buttonColored("Delete", { 0.45f, 0.14f, 0.14f }, { 1, 0.85f, 0.85f })) {
        addLog(0, "Deleted: %s", e.name);
        std::vector<int> audioIds; g.scene.collectSubtree(e.id, audioIds);
        for (int audioId : audioIds) g.audio.stop(audioId);
        if (g.componentResetMenuEntity == e.id) {
            g.componentResetMenuEntity = 0;
            g.componentResetMenuKind = -1;
        }
        g.scene.removeEntity(e.id);
        g.selectedId = 0;
        entityDeleted = true;
    }
    if (!entityDeleted) {
        // The popup blocks the Details widgets behind it, but must itself read
        // the real pointer input so Reset and click-outside can close it.
        ui.setInteractionBlocked(detailsWasBlocked);
        drawComponentResetMenu(ui, e);
    }
    ui.setInteractionBlocked(detailsWasBlocked);
#endif
    ui.setInteractionBlocked(detailsWasBlocked);
    drawComponentResetMenu(ui, e);
    ui.setInteractionBlocked(detailsWasBlocked);
}

static void drawLogContent(UI& ui) {
    for (const auto& L : g.logs) {
        Vec3 c = L.customColor?L.color:L.level == 1 ? Vec3{ 0.55f, 0.9f, 0.65f }
               : L.level == 2 ? Vec3{ 1.0f, 0.55f, 0.55f }
               : Vec3{ 0.62f, 0.68f, 0.76f };
        // Text itself has no separate alpha channel in the panel API; blend
        // its RGB towards the panel tone to preserve the selected opacity.
        if(L.customColor)c=c*L.alpha+Vec3{.09f,.10f,.115f}*(1-L.alpha);
        ui.label(L.text, c);
    }
    if (g.logScrollPending) {
        ui.scrollToBottom("dock_log");
        ui.scrollToBottom("float_log");
        g.logScrollPending = false;
    }
    // "Clear" button pinned to the panel's top-right, above the scrolling text so
    // it stays reachable even when the log has auto-scrolled to the bottom
    Renderer* r = ui.r;
    UIRect panel = ui.panelInner();
    const UIInput& in = ui.input();
    // Box sized from the text with an equal gap on every side, so the label is
    // fully covered by the background and the padding matches all round.
    const float pad = 6;
    float tw = r->textWidth("Clear"), th = r->fontHeight();
    float bw = tw + pad * 2, bh = th + pad * 2;
    float bx = panel.x + panel.w - bw - 12, by = panel.y + 5;
    bool hover = in.mouseX >= bx && in.mouseX < bx + bw && in.mouseY >= by && in.mouseY < by + bh;
    r->setUIScissor(0, 0, 0, 0, false);
    r->drawRectPx(bx, by, bw, bh, hover ? Vec3{ .34f, .17f, .17f } : Vec3{ .22f, .13f, .13f }, 1);
    r->drawTextLine(bx + pad, by + pad, "Clear", hover ? Vec3{ 1, .86f, .86f } : Vec3{ .86f, .72f, .72f }, 1);
    ui.registerBlockingRect({ bx, by, bw, bh });
    if (hover && in.mouseReleased) g.logs.clear();
    ui.reclipPanel();
}

// ═══ content browser: file operations ═══
static std::string relJoin(const std::string& a, const std::string& b) {
    return a.empty() ? b : a + "\\" + b;
}
static std::string relAbs(const std::string& rel) {
    return rel.empty() ? g.projectDir : g.projectDir + "\\" + rel;
}
static std::string relParent(const std::string& rel) {
    size_t sep = rel.find_last_of('\\');
    return sep == std::string::npos ? "" : rel.substr(0, sep);
}

// first free name in dirAbs, appending _copia / _copia2 ... when taken
static std::string uniqueDest(const std::string& dirAbs, const std::string& name) {
    fs::path p(name);
    std::string stem = p.stem().string(), ext = p.extension().string();
    std::string cand = name;
    std::error_code ec;
    for (int i = 1; fs::exists(dirAbs + "\\" + cand, ec); i++) {
        cand = stem + "_copy" + (i > 1 ? std::to_string(i) : "") + ext;
    }
    return cand;
}

static void browserDeleteNoConfirm(const std::string& name) {
    std::error_code ec;
    std::string deletedRel = relJoin(g.curRel, name);
    fs::remove_all(curDirAbs() + "\\" + name, ec);
    if (!ec) for (Entity& e : g.scene.entities) {
        auto deleted = [&](const char* path) {
            std::string p = path;
            return p == deletedRel || (p.size() > deletedRel.size() && p.rfind(deletedRel + "\\", 0) == 0);
        };
        for (int componentIndex = entityBlueprintCount(e) - 1; componentIndex >= 0; componentIndex--)
            if (deleted(entityBlueprintPath(e, componentIndex))) removeBlueprintComponent(e, componentIndex);
        if (deleted(e.audioClip)) {
            g.audio.stop(e.id);
            e.audioClip[0] = 0;
        }
        if (deleted(e.audioClass)) e.audioClass[0] = 0;
        if (deleted(e.audioAttenuation)) e.audioAttenuation[0] = 0;
        if (deleted(e.audioConcurrency)) e.audioConcurrency[0] = 0;
        if (deleted(e.animatorController)) { e.animatorController[0]=0; e.hasAnimator=false; }
    }
    if (!ec && (g.audioAssetEditRel == deletedRel ||
        (g.audioAssetEditRel.size() > deletedRel.size() && g.audioAssetEditRel.rfind(deletedRel + "\\", 0) == 0))) {
        g.audioAssetEditKind = -1;
        g.audioAssetEditRel.clear();
    }
    if (!ec && (g.enumAssetEditRel == deletedRel ||
        (g.enumAssetEditRel.size() > deletedRel.size() && g.enumAssetEditRel.rfind(deletedRel + "\\", 0) == 0)))
        g.enumAssetEditRel.clear();
    addLog(ec ? 2 : 0, ec ? "Could not delete %s." : "Deleted: %s", name.c_str());
    scanBrowser();
}

static void browserDelete(const std::string& name) {
    char msg[320];
    snprintf(msg, sizeof(msg), "Eliminare definitivamente \"%s\"?", name.c_str());
    if (MessageBoxA(g.hwnd, msg, "Confirm deletion", MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) != IDYES) return;
    browserDeleteNoConfirm(name);
}

// Delete every currently-selected asset in the browser view, asking a single
// confirmation up front (Delete key in the Content panel / drawer).
static void browserDeleteSelected() {
    std::vector<std::string> names;   // filenames in the current folder
    for (const std::string& rel : g.browserSelected) {
        std::string nm = fs::path(rel).filename().string();
        if (!nm.empty() && nm != "..") names.push_back(nm);
    }
    if (names.empty()) return;
    char msg[320];
    if (names.size() == 1)
        snprintf(msg, sizeof(msg), "Eliminare definitivamente \"%s\"?", names[0].c_str());
    else
        snprintf(msg, sizeof(msg), "Permanently delete %d selected items?", (int)names.size());
    if (MessageBoxA(g.hwnd, msg, "Confirm deletion", MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) != IDYES) return;
    for (const std::string& nm : names) browserDeleteNoConfirm(nm);
    browserClearSelection();
    scanBrowser();
}

static void browserDuplicate(const std::string& name) {
    std::error_code ec;
    std::string dst = uniqueDest(curDirAbs(), name);
    fs::copy(curDirAbs() + "\\" + name, curDirAbs() + "\\" + dst, fs::copy_options::recursive, ec);
    addLog(ec ? 2 : 1, ec ? "Duplication failed." : "Duplicated: %s", dst.c_str());
    scanBrowser();
    if (!ec) browserSetSingleSelectionRel(relJoin(g.curRel, dst));
}

static void browserPaste() {
    std::error_code ec;
    if (g.fileClipboard.empty() || !fs::exists(g.fileClipboard, ec)) {
        addLog(2, "Niente da incollare.");
        return;
    }
    std::string name = fs::path(g.fileClipboard).filename().string();
    std::string dst = uniqueDest(curDirAbs(), name);
    fs::copy(g.fileClipboard, curDirAbs() + "\\" + dst, fs::copy_options::recursive, ec);
    addLog(ec ? 2 : 1, ec ? "Paste failed." : "Pasted: %s", dst.c_str());
    scanBrowser();
    if (!ec) browserSetSingleSelectionRel(relJoin(g.curRel, dst));
}

static bool importableAssetExtension(std::string ext) {
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return (char)tolower(c); });
    return ext == ".obj" || ext == ".fbx" || ext == ".gltf" || ext == ".glb" ||
           ext == ".dae" || ext == ".3ds" || ext == ".stl" || ext == ".png" ||
           ext == ".mp3" || ext == ".wav" || ext == ".ogg";
}

static void browserImportAssets() {
    char paths[32768] = {};
    static const char FILTER[] =
        "Asset supportati (*.obj;*.fbx;*.gltf;*.glb;*.dae;*.3ds;*.stl;*.png;*.mp3;*.wav;*.ogg)\0"
        "*.obj;*.fbx;*.gltf;*.glb;*.dae;*.3ds;*.stl;*.png;*.mp3;*.wav;*.ogg\0"
        "Mesh 3D\0*.obj;*.fbx;*.gltf;*.glb;*.dae;*.3ds;*.stl\0"
        "Texture PNG\0*.png\0"
        "Audio\0*.mp3;*.wav;*.ogg\0\0";
    OPENFILENAMEA ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = g.hwnd;
    ofn.lpstrFile = paths;
    ofn.nMaxFile = sizeof(paths);
    ofn.lpstrFilter = FILTER;
    ofn.nFilterIndex = 1;
    ofn.Flags = OFN_EXPLORER | OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_ALLOWMULTISELECT | OFN_NOCHANGEDIR;
    if (!GetOpenFileNameA(&ofn)) return;

    std::vector<fs::path> sources;
    char* first = paths;
    char* next = first + strlen(first) + 1;
    if (!*next) {
        sources.emplace_back(first);
    } else {
        fs::path dir(first);
        while (*next) {
            sources.push_back(dir / next);
            next += strlen(next) + 1;
        }
    }

    int imported = 0, rejected = 0;
    std::string lastName;
    for (const fs::path& source : sources) {
        if (!importableAssetExtension(source.extension().string())) { rejected++; continue; }
        std::string name = uniqueDest(curDirAbs(), source.filename().string());
        fs::path dest = fs::path(curDirAbs()) / name;
        std::error_code ec;
        fs::copy_file(source, dest, fs::copy_options::none, ec);
        if (ec) {
            addLog(2, "Import failed for %s: %s", source.filename().string().c_str(), ec.message().c_str());
            continue;
        }
        imported++;
        lastName = name;
    }
    scanBrowser();
    if (!lastName.empty()) browserSetSingleSelectionRel(relJoin(g.curRel, lastName));
    if (imported) addLog(1, "Imported %d assets into %s.", imported, g.curRel.empty() ? g.projectName.c_str() : g.curRel.c_str());
    if (rejected) addLog(2, "%d files ignored: unsupported format.", rejected);
}

static void browserMove(const std::string& name, const std::string& destRel) {
    std::string srcRel = relJoin(g.curRel, name);
    if (destRel == g.curRel) return;
    if (destRel == srcRel || destRel.rfind(srcRel + "\\", 0) == 0) {
        addLog(2, "Cannot move a folder into itself.");
        return;
    }
    std::error_code ec;
    std::string dstName = uniqueDest(relAbs(destRel), name);
    std::string dstRel = relJoin(destRel, dstName);
    std::string dst = relAbs(destRel) + "\\" + dstName;
    fs::rename(curDirAbs() + "\\" + name, dst, ec);
    if (!ec) {
        for (Entity& e : g.scene.entities) {
            auto moveRef = [&](char* path, size_t cap) {
                std::string p = path;
                if (p == srcRel || (p.size() > srcRel.size() && p.rfind(srcRel + "\\", 0) == 0)) {
                    p = dstRel + p.substr(srcRel.size());
                    snprintf(path, cap, "%s", p.c_str());
                }
            };
            moveRef(e.audioClip, sizeof(e.audioClip));
            moveRef(e.audioClass, sizeof(e.audioClass));
            moveRef(e.audioAttenuation, sizeof(e.audioAttenuation));
            moveRef(e.audioConcurrency, sizeof(e.audioConcurrency));
            moveRef(e.animatorController, sizeof(e.animatorController));
        }
        if (g.audioAssetEditRel == srcRel ||
            (g.audioAssetEditRel.size() > srcRel.size() && g.audioAssetEditRel.rfind(srcRel + "\\", 0) == 0))
            g.audioAssetEditRel = dstRel + g.audioAssetEditRel.substr(srcRel.size());
        if (g.enumAssetEditRel == srcRel ||
            (g.enumAssetEditRel.size() > srcRel.size() && g.enumAssetEditRel.rfind(srcRel + "\\", 0) == 0))
            g.enumAssetEditRel = dstRel + g.enumAssetEditRel.substr(srcRel.size());
    }
    addLog(ec ? 2 : 1, ec ? "Move failed." : "Moved to: %s",
           destRel.empty() ? g.projectName.c_str() : destRel.c_str());
    scanBrowser();
}

static bool relHasPrefix(const std::string& value, const std::string& prefix) {
    return value == prefix || (value.size() > prefix.size() && value.rfind(prefix + "\\", 0) == 0);
}

static std::string remapRelPrefix(const std::string& value, const std::string& oldRel, const std::string& newRel) {
    return relHasPrefix(value, oldRel) ? newRel + value.substr(oldRel.size()) : value;
}

static bool browserRenamePath(const std::string& sourceRel, const std::string& requestedName) {
    std::error_code typeEc;
    bool isDirectory = fs::is_directory(relAbs(sourceRel), typeEc);
    std::string name = requestedName;
    while (!name.empty() && (name.front() == ' ' || name.front() == '\t')) name.erase(name.begin());
    while (!name.empty() && (name.back() == ' ' || name.back() == '\t')) name.pop_back();
    if (name.empty() || name == "." || name == ".." || name.find_first_of("<>:\"/\\|?*") != std::string::npos) {
        addLog(2, "Nome non valido.");
        return false;
    }
    if (!isDirectory) {
        std::string originalExt = fs::path(sourceRel).extension().string();
        std::string requestedExt = fs::path(name).extension().string();
        if (requestedExt.empty()) name += originalExt;
        else if (_stricmp(requestedExt.c_str(), originalExt.c_str()) != 0) {
            addLog(2, "The asset extension must stay %s.", originalExt.c_str());
            return false;
        }
    }
    std::string parent = relParent(sourceRel);
    std::string newRel = relJoin(parent, name);
    if (newRel == sourceRel) return true;
    std::error_code ec;
    if (fs::exists(relAbs(newRel), ec)) {
        addLog(2, "An item named '%s' already exists.", name.c_str());
        return false;
    }
    fs::rename(relAbs(sourceRel), relAbs(newRel), ec);
    if (ec) {
        addLog(2, "Rename failed: %s", ec.message().c_str());
        return false;
    }

    // Keep open documents, scene assignments and other drawers on the new path.
    g.curRel = remapRelPrefix(g.curRel, sourceRel, newRel);
    for (ContentDrawer& d : g.drawers) d.st.curRel = remapRelPrefix(d.st.curRel, sourceRel, newRel);
    for (auto& ed : g.bpDocs) if (ed) {
        ed->curPath = remapRelPrefix(ed->curPath, sourceRel, newRel);
        ed->graph.parentAsset = remapRelPrefix(ed->graph.parentAsset, sourceRel, newRel);
        ed->graph.defaultPawnClass = remapRelPrefix(ed->graph.defaultPawnClass, sourceRel, newRel);
        ed->graph.playerControllerClass = remapRelPrefix(ed->graph.playerControllerClass, sourceRel, newRel);
        for (std::string& iface : ed->graph.interfaceAssets)
            iface = remapRelPrefix(iface, sourceRel, newRel);
        for (BPVarDef& var : ed->graph.vars) {
            std::string cls = var.refClass;
            const std::string prefix = "blueprint:";
            if (cls.rfind(prefix, 0) == 0) {
                std::string path = remapRelPrefix(cls.substr(prefix.size()), sourceRel, newRel);
                snprintf(var.refClass, sizeof(var.refClass), "%s%s", prefix.c_str(), path.c_str());
            }
            std::string enumPath = remapRelPrefix(var.enumAsset, sourceRel, newRel);
            snprintf(var.enumAsset, sizeof(var.enumAsset), "%s", enumPath.c_str());
        }
        auto remapNodeAssets = [&](BPCanvas& canvas) {
            for (BPNode& node : canvas.nodes) {
                if (node.def == BP_GET_ALL_WITH_CLASS) {
                    const std::string prefix = "blueprint:";
                    std::string value = node.sname;
                    if (value.rfind(prefix, 0) == 0) {
                        std::string path = remapRelPrefix(value.substr(prefix.size()), sourceRel, newRel);
                        snprintf(node.sname, sizeof(node.sname), "%s%s", prefix.c_str(), path.c_str());
                    }
                    continue;
                }
                if (node.def != BP_SPAWN_PREFAB && node.def != BP_SELECT_ENUM && node.def != BP_SWITCH_ENUM) continue;
                std::string path = remapRelPrefix(node.sname, sourceRel, newRel);
                snprintf(node.sname, sizeof(node.sname), "%s", path.c_str());
            }
        };
        for (BPFunc& graphCanvas : ed->graph.graphs) remapNodeAssets(graphCanvas.body);
        for (BPFunc& functionCanvas : ed->graph.funcs) remapNodeAssets(functionCanvas.body);
    }
    for (auto& ed : g.curveDocs) if (ed) ed->curPath = remapRelPrefix(ed->curPath, sourceRel, newRel);
    g.animationEditRel = remapRelPrefix(g.animationEditRel, sourceRel, newRel);
    g.animatorEditRel = remapRelPrefix(g.animatorEditRel, sourceRel, newRel);
    for (AnimatorState& state : g.animatorEdit.states)
        state.clip = remapRelPrefix(state.clip, sourceRel, newRel);
    // Controller assets store direct clip references, so renaming an animation
    // or a folder updates every controller instead of leaving string links stale.
    std::error_code controllerEc;
    for (fs::recursive_directory_iterator it(g.projectDir, fs::directory_options::skip_permission_denied, controllerEc), end;
         !controllerEc && it != end; it.increment(controllerEc)) {
        if (!it->is_regular_file(controllerEc) || _stricmp(it->path().extension().string().c_str(), ".animctrl") != 0) continue;
        std::string controllerData; AnimatorControllerAsset controller;
        if (!readFile(it->path().string(), controllerData) || !controller.deserialize(controllerData)) continue;
        bool changed = false;
        for (AnimatorState& state : controller.states) {
            std::string remapped = remapRelPrefix(state.clip, sourceRel, newRel);
            if (remapped != state.clip) { state.clip = remapped; changed = true; }
        }
        if (changed) writeFile(it->path().string(), controller.serialize());
    }
    for (auto& e : g.scene.entities) {
        std::string p = e.graphPath;
        p = remapRelPrefix(p, sourceRel, newRel);
        snprintf(e.graphPath, sizeof(e.graphPath), "%s", p.c_str());
        for (BlueprintComponentDef& component : e.additionalBlueprints)
            component.graphPath = remapRelPrefix(component.graphPath, sourceRel, newRel);
        p = e.audioClip;
        p = remapRelPrefix(p, sourceRel, newRel);
        snprintf(e.audioClip, sizeof(e.audioClip), "%s", p.c_str());
        p = e.audioClass; p = remapRelPrefix(p, sourceRel, newRel);
        snprintf(e.audioClass, sizeof(e.audioClass), "%s", p.c_str());
        p = e.audioAttenuation; p = remapRelPrefix(p, sourceRel, newRel);
        snprintf(e.audioAttenuation, sizeof(e.audioAttenuation), "%s", p.c_str());
        p = e.audioConcurrency; p = remapRelPrefix(p, sourceRel, newRel);
        snprintf(e.audioConcurrency, sizeof(e.audioConcurrency), "%s", p.c_str());
        p = e.animatorController; p = remapRelPrefix(p, sourceRel, newRel);
        snprintf(e.animatorController, sizeof(e.animatorController), "%s", p.c_str());
    }
    g.audioAssetEditRel = remapRelPrefix(g.audioAssetEditRel, sourceRel, newRel);
    g.enumAssetEditRel = remapRelPrefix(g.enumAssetEditRel, sourceRel, newRel);
    std::string oldAbs = relAbs(sourceRel), newAbs = relAbs(newRel);
    g.scene.gameModePath = remapRelPrefix(g.scene.gameModePath, sourceRel, newRel);
    std::string remappedInstance = remapRelPrefix(g.gameInstanceAsset, sourceRel, newRel);
    if (remappedInstance != g.gameInstanceAsset) { g.gameInstanceAsset = remappedInstance; saveGameplayProjectSettings(); }
    std::string level = g.projectPath;
    if (level == oldAbs || (level.size() > oldAbs.size() && level.rfind(oldAbs + "\\", 0) == 0))
        snprintf(g.projectPath, sizeof(g.projectPath), "%s", (newAbs + level.substr(oldAbs.size())).c_str());

    g.graphCache.clear();
    g.curveCache.clear();
    g.bpEditCache.clear();
    scanBrowser();
    browserSetSingleSelectionRel(newRel);
    addLog(1, "%s renamed: %s", isDirectory ? "Folder" : "Asset", newRel.c_str());
    return true;
}

static void browserStartRename(const std::string& rel) {
    if (rel.empty()) return;
    g.renameRel = rel;
    snprintf(g.renameBuf, sizeof(g.renameBuf), "%s", fs::path(rel).filename().string().c_str());
    g.renameOpen = true;
    g.renameAutoFocus = true;
    g.browserCtx = 0;
}

static bool browserDrawInlineRename(UI& ui, const char* id, const UIRect& rc) {
    bool autoFocus = g.renameAutoFocus;
    bool hadFocus = autoFocus || ui.inputFocused(id);
    const UIInput& in = ui.input();
    ui.textInputRect(id, g.renameBuf, sizeof(g.renameBuf), rc, autoFocus);
    g.renameAutoFocus = false;
    if (hadFocus && in.keyEnter) {
        std::string rel = g.renameRel;
        if (browserRenamePath(rel, g.renameBuf)) {
            g.renameOpen = false;
            g.renameRel.clear();
            g.renameAutoFocus = false;
            return true;
        }
        g.renameAutoFocus = true;
    } else if (in.keyEscape) {
        g.renameOpen = false;
        g.renameRel.clear();
        g.renameAutoFocus = false;
        return true;
    }
    return false;
}

static void browserCreateAsset(int kind) {
    // 0 Actor BP, 1 Interface, 2 Curve, 3..6 existing assets, 7..10 framework BP classes.
    const char* base = kind == 0 ? "NewBlueprint.bp"
                     : kind == 1 ? "NewInterface.bpi"
                     : kind == 2 ? "NewCurve.curve"
                     : kind == 3 ? "NewAudioClass.aclass"
                     : kind == 4 ? "NewAttenuation.atten"
                     : kind == 5 ? "NewConcurrency.concurrency"
                     : kind == 6 ? "NewEnum.enum"
                     : kind == 7 ? "NewGameMode.bp"
                     : kind == 8 ? "NewGameInstance.bp"
                     : kind == 9 ? "NewPlayerController.bp"
                     : kind == 10 ? "NewSaveGame.bp"
                     : kind == 11 ? "NewAnimation.anim"
                     : kind == 12 ? "NewAnimator.animctrl"
                     : kind == 13 ? "NewMaterial.mat"
                                  : "NewWidget.wgt";
    std::string name = uniqueDest(curDirAbs(), base);
    std::string abs = curDirAbs() + "\\" + name;
    std::string data;
    if (kind == 11) {
        data = AnimationClipAsset{}.serialize();
    } else if (kind == 12) {
        data = AnimatorControllerAsset{}.serialize();
    } else if (kind == 2) {
        data = CurveAsset{}.serialize();
    } else if (kind == 3) {
        data = AudioClassAsset{}.serialize();
    } else if (kind == 4) {
        data = AudioAttenuationAsset{}.serialize();
    } else if (kind == 5) {
        data = AudioConcurrencyAsset{}.serialize();
    } else if (kind == 6) {
        data = BPEnumAsset{}.serialize();
    } else if (kind == 13) {
        data = MaterialAsset{}.serialize();
    } else if (kind == 14) {
        data = WidgetAsset{}.serialize();
    } else {
        BPGraph asset;
        asset.clear();
        asset.ensureDefaults();
        if (kind == 0 || kind >= 7) {
            asset.classKind = kind == 7 ? BP_CLASS_GAMEMODE
                            : kind == 8 ? BP_CLASS_GAMEINSTANCE
                            : kind == 9 ? BP_CLASS_PLAYERCONTROLLER
                            : kind == 10 ? BP_CLASS_SAVEGAME : BP_CLASS_ACTOR;
            asset.main().addNode(BP_EV_START, 40, 60);
        } else {
            // An interface asset stores function signatures in the same proven
            // format as Blueprint functions. Implementations can import them.
            BPFunc hiddenCanvas;
            snprintf(hiddenCanvas.name, sizeof(hiddenCanvas.name), "EventGraph");
            hiddenCanvas.ins.clear(); hiddenCanvas.outs.clear(); hiddenCanvas.body.clear();
            asset.graphs.clear();
            asset.graphs.push_back(std::move(hiddenCanvas));
            BPFunc fn;
            snprintf(fn.name, sizeof(fn.name), "Execute");
            fn.ins.clear();
            fn.outs.clear();
            fn.body.addNode(BP_FN_ENTRY, 40, 70);
            asset.funcs.push_back(fn);
        }
        data = asset.serialize();
    }
    if (!writeFile(abs, data)) {
        addLog(2, "Could not create the asset: %s", name.c_str());
        return;
    }
    std::string rel = relJoin(g.curRel, name);
    scanBrowser();
    browserSetSingleSelectionRel(rel);
    addLog(1, "Asset creato: %s", rel.c_str());
    if (kind == 11) {
        restoreAnimationPreview();
        g.animationEditRel = rel; g.animationEdit = AnimationClipAsset{};
        g.animationTime = 0; animationClearKeySelection();
        g.animationTimelineStart = 0; g.animationTimelinePixelsPerSecond = 100;
        g.animationTimelinePanning = false; g.animationDraggingKey = -1;
        DockWindow* w = g.dock.find("animation"); if (w) { if (!w->open) g.dock.toggle("animation"); g.dock.setActive("animation"); }
    } else if (kind == 12) {
        g.animatorEditRel = rel; g.animatorEdit = AnimatorControllerAsset{};
        g.animatorSelectedState = g.animatorEdit.defaultState; g.animatorSelectedTransition = -1; g.animatorSection = 0;
        DockWindow* w = g.dock.find("animator"); if (w) { if (!w->open) g.dock.toggle("animator"); g.dock.setActive("animator"); }
    } else if (kind == 2) openCurveDoc(abs, rel);
    else if (kind >= 3 && kind <= 5) openAudioSettingsAsset(rel, kind - 3);
    else if (kind == 6) openEnumAsset(rel);
    else if (kind == 13) openMaterialDoc(abs, rel);
    else if (kind == 14) openWidgetDoc(abs, rel);
    else openBlueprintDoc(abs, rel);
}

static void browserCreateChildBlueprint(const std::string& parentRel) {
    std::string data;
    BPGraph parent;
    if (!readFile(g.projectDir + "\\" + parentRel, data) || !parent.deserialize(data)) {
        addLog(2, "Could not read the parent Blueprint: %s", parentRel.c_str());
        return;
    }
    std::string base = fs::path(parentRel).stem().string() + "_Child.bp";
    std::string name = uniqueDest(curDirAbs(), base);
    std::string rel = relJoin(g.curRel, name);
    BPGraph child;
    child.clear();
    child.ensureDefaults();
    child.classKind = parent.classKind;
    child.parentAsset = parentRel;
    child.main().addNode(BP_EV_START, 40, 60);
    if (!writeFile(g.projectDir + "\\" + rel, child.serialize())) {
        addLog(2, "Could not create the Child Blueprint.");
        return;
    }
    scanBrowser();
    browserSetSingleSelectionRel(rel);
    openBlueprintDoc(g.projectDir + "\\" + rel, rel);
    addLog(1, "Child Blueprint created: %s (parent: %s)", rel.c_str(), parentRel.c_str());
}

static void browserImplementInterface(const std::string& name) {
    BPEditor* target = activeBP();
    if (!target || fs::path(target->curPath).extension() == ".bpi") {
        addLog(2, "Open the Blueprint that should implement the interface first.");
        return;
    }
    std::string interfaceRel = relJoin(g.curRel, name);
    target->implementInterfaceAsset(interfaceRel);
}

// double-click / "Open" action on a tile
static void browserOpen(const std::string& name, int icon) {
    if (icon == 0) {
        if (name == "..") g.curRel = relParent(g.curRel);
        else g.curRel = relJoin(g.curRel, name);
        browserClearSelection();
        scanBrowser();
    } else if (icon == 2) {
        openPrefabEditor(relJoin(g.curRel,name));
    } else if (icon == 3) {
        openProjectFile(curDirAbs() + "\\" + name);
    } else if (icon == 4 || icon == 5) {
        std::string rel = relJoin(g.curRel, name);
        if (openBlueprintDoc(curDirAbs() + "\\" + name, rel) >= 0) {
            addLog(1, "%s aperta: %s", icon == 5 ? "Interface" : "Blueprint", rel.c_str());
        }
    } else if (icon == 6) {
        std::string rel = relJoin(g.curRel, name);
        if (openCurveDoc(curDirAbs() + "\\" + name, rel) >= 0) addLog(1, "Curve aperta: %s", rel.c_str());
    } else if (icon == 9) {
        g.audio.stop(-1);
        if (g.audio.play(-1, curDirAbs() + "\\" + name, false, 1.0f)) addLog(0, "Audio preview: %s", name.c_str());
        else addLog(2, "Could not play %s: %s", name.c_str(), g.audio.lastError().c_str());
    } else if (icon >= 10 && icon <= 12) {
        std::string rel = relJoin(g.curRel, name);
        if (openAudioSettingsAsset(rel, icon - 10)) addLog(1, "Audio asset opened: %s", rel.c_str());
        else addLog(2, "Invalid audio asset: %s", rel.c_str());
    } else if (icon == 13) {
        std::string rel = relJoin(g.curRel, name);
        if (openEnumAsset(rel)) addLog(1, "Enum aperto: %s", rel.c_str());
        else addLog(2, "Invalid Enum: %s", rel.c_str());
    } else if (icon == 16) {
        std::string rel = relJoin(g.curRel, name);
        if (openMaterialDoc(curDirAbs() + "\\" + name, rel) >= 0) addLog(1, "Material opened: %s", rel.c_str());
    } else if (icon == 17) {
        std::string rel = relJoin(g.curRel, name);
        if (openWidgetDoc(curDirAbs() + "\\" + name, rel) >= 0) addLog(1, "Widget opened: %s", rel.c_str());
    } else if (icon == 14) {
        std::string rel = relJoin(g.curRel, name), data;
        AnimationClipAsset opened;
        if (readFile(relAbs(rel), data) && opened.deserialize(data)) {
            restoreAnimationPreview();
            g.animationEdit = std::move(opened);
            g.animationEditRel = rel; g.animationTime = 0; animationClearKeySelection();
            g.animationTimelineStart = 0; g.animationTimelinePixelsPerSecond = 100;
            g.animationTimelinePanning = false; g.animationDraggingKey = -1;
            DockWindow* w = g.dock.find("animation");
            if (w) { if (!w->open) g.dock.toggle("animation"); g.dock.setActive("animation"); }
            addLog(1, "Animation Clip opened: %s", rel.c_str());
        } else addLog(2, "Invalid Animation Clip: %s", rel.c_str());
    } else if (icon == 15) {
        std::string rel = relJoin(g.curRel, name), data;
        if (readFile(relAbs(rel), data) && g.animatorEdit.deserialize(data)) {
            g.animatorEditRel = rel; g.animatorSelectedState = g.animatorEdit.defaultState; g.animatorSelectedTransition = -1; g.animatorConnectFrom = 0; g.animatorSection = 0;
            DockWindow* w = g.dock.find("animator");
            if (w) { if (!w->open) g.dock.toggle("animator"); g.dock.setActive("animator"); }
            addLog(1, "Animator Controller opened: %s", rel.c_str());
        } else addLog(2, "Invalid Animator Controller: %s", rel.c_str());
    }
}

static void drawFolderNode(UI& ui, int idx, int depth) {
    // copy: a click below triggers scanBrowser() which rebuilds g.folders
    App::FolderNode node = g.folders[idx];
    bool hasKids = !node.kids.empty();
    bool expanded = !g.folderCollapsed.count(node.rel);
    bool selected = g.curRel == node.rel;
    std::string id = "fold_" + node.rel;
    bool dropHi = g.dragItemActive && g.dropHoverRel == node.rel;
    bool renaming = g.renameOpen && g.renameRel == node.rel;
    std::string shownName = renaming ? std::string() : node.name;
    int fl = ui.treeItem(id.c_str(), shownName, depth, hasKids, expanded, selected || renaming, dropHi,
                         false, expanded ? 1 : 0, g.folderColors.count(node.rel)?&g.folderColors[node.rel]:nullptr);
    if (renaming) {
        UIRect row = ui.lastItemRect();
        float indent = depth * 16.0f;
        UIRect edit = { row.x + indent + 39.0f, row.y + 1.0f,
                        (std::max)(40.0f, row.w - indent - 43.0f), row.h - 2.0f };
        bool closed = browserDrawInlineRename(ui, (id + "_rename").c_str(), edit);
        if (closed) return;
        if (expanded) {
            std::vector<int> kids = node.kids;
            for (int k : kids) {
                if (k < (int)g.folders.size()) drawFolderNode(ui, k, depth + 1);
            }
        }
        return;
    }
    if (g.dragItemActive && (fl & UI::TREE_HOVERED)) {
        g.dropHoverRel = node.rel;
        g.dropHoverValid = true;
    }
    if ((fl & UI::TREE_RCLICKED) && !g.dragItemActive) {
        const UIInput& in = ui.input();
        g.browserCtx = 3;
        g.ctxName = node.name;
        g.ctxRelPath = node.rel;
        g.ctxIcon = 0;
        g.ctxX = in.mouseX;
        g.ctxY = in.mouseY;
        g.ctxMoveOpen = false;
        g.ctxCreateOpen = false;
    } else if (fl & UI::TREE_TOGGLED) {
        if (expanded) g.folderCollapsed.insert(node.rel);
        else g.folderCollapsed.erase(node.rel);
    } else if ((fl & UI::TREE_CLICKED) && !g.dragItemActive) {
        g.curRel = node.rel;
        browserClearSelection();
        scanBrowser();
    }
    if (expanded) {
        // copy: scanBrowser may rebuild g.folders mid-iteration after a click
        std::vector<int> kids = node.kids;
        for (int k : kids) {
            if (k < (int)g.folders.size()) drawFolderNode(ui, k, depth + 1);
        }
    }
}

// swap a browser view's navigation in/out of the global working fields, so the
// unchanged drawContenutiContent (which uses g.*) can serve any drawer
static void browserLoad(const BrowserState& s) {
    g.curRel = s.curRel;
    g.curDirs = s.curDirs; g.curPfbs = s.curPfbs; g.curImps = s.curImps; g.curBps = s.curBps;
    g.curBpis = s.curBpis; g.curCurves = s.curCurves; g.curAnimations = s.curAnimations; g.curAnimators = s.curAnimators;
    g.curMeshes = s.curMeshes; g.curTextures = s.curTextures; g.curAudio = s.curAudio;
    g.curAudioClasses = s.curAudioClasses; g.curAudioAttenuations = s.curAudioAttenuations;
    g.curAudioConcurrencies = s.curAudioConcurrencies;
    g.curEnums = s.curEnums;
    g.curMaterials = s.curMaterials;
    g.curWidgets = s.curWidgets;
    g.browserSel = s.browserSel;
    g.browserSelected = s.browserSelected;
    g.browserSelectionAnchor = s.browserSelectionAnchor;
    g.browserSplit = s.browserSplit; g.browserTileHeight = s.browserTileHeight; g.browserSplitDrag = s.browserSplitDrag;
    memcpy(g.pathEdit, s.pathEdit, sizeof(g.pathEdit)); g.pathEditSynced = s.pathEditSynced;
    g.browserCtx = s.browserCtx; g.ctxName = s.ctxName; g.ctxRelPath = s.ctxRelPath; g.ctxIcon = s.ctxIcon;
    g.ctxX = s.ctxX; g.ctxY = s.ctxY; g.ctxMoveOpen = s.ctxMoveOpen; g.ctxCreateOpen = s.ctxCreateOpen; g.ctxMoveScroll = s.ctxMoveScroll;
    g.renameOpen = s.renameOpen; g.renameRel = s.renameRel; memcpy(g.renameBuf, s.renameBuf, sizeof(g.renameBuf));
    g.renameAutoFocus = s.renameAutoFocus;
    g.dragItem = s.dragItem; g.dragItemIcon = s.dragItemIcon; g.dragItemActive = s.dragItemActive;
    g.dragItemX = s.dragItemX; g.dragItemY = s.dragItemY;
    g.dropHoverRel = s.dropHoverRel; g.dropHoverValid = s.dropHoverValid; g.dropTileName = s.dropTileName;
}
static void browserSave(BrowserState& s) {
    s.curRel = g.curRel;
    s.curDirs = g.curDirs; s.curPfbs = g.curPfbs; s.curImps = g.curImps; s.curBps = g.curBps;
    s.curBpis = g.curBpis; s.curCurves = g.curCurves; s.curAnimations = g.curAnimations; s.curAnimators = g.curAnimators;
    s.curMeshes = g.curMeshes; s.curTextures = g.curTextures; s.curAudio = g.curAudio;
    s.curAudioClasses = g.curAudioClasses; s.curAudioAttenuations = g.curAudioAttenuations;
    s.curAudioConcurrencies = g.curAudioConcurrencies;
    s.curEnums = g.curEnums;
    s.curMaterials = g.curMaterials;
    s.curWidgets = g.curWidgets;
    s.browserSel = g.browserSel;
    s.browserSelected = g.browserSelected;
    s.browserSelectionAnchor = g.browserSelectionAnchor;
    s.browserSplit = g.browserSplit; s.browserTileHeight = g.browserTileHeight; s.browserSplitDrag = g.browserSplitDrag;
    memcpy(s.pathEdit, g.pathEdit, sizeof(s.pathEdit)); s.pathEditSynced = g.pathEditSynced;
    s.browserCtx = g.browserCtx; s.ctxName = g.ctxName; s.ctxRelPath = g.ctxRelPath; s.ctxIcon = g.ctxIcon;
    s.ctxX = g.ctxX; s.ctxY = g.ctxY; s.ctxMoveOpen = g.ctxMoveOpen; s.ctxCreateOpen = g.ctxCreateOpen; s.ctxMoveScroll = g.ctxMoveScroll;
    s.renameOpen = g.renameOpen; s.renameRel = g.renameRel; memcpy(s.renameBuf, g.renameBuf, sizeof(s.renameBuf));
    s.renameAutoFocus = g.renameAutoFocus;
    s.dragItem = g.dragItem; s.dragItemIcon = g.dragItemIcon; s.dragItemActive = g.dragItemActive;
    s.dragItemX = g.dragItemX; s.dragItemY = g.dragItemY;
    s.dropHoverRel = g.dropHoverRel; s.dropHoverValid = g.dropHoverValid; s.dropTileName = g.dropTileName;
}

static void drawContenutiContent(UI& ui) {
    const Vec3 dim = { 0.55f, 0.59f, 0.66f };
    const Vec3 accent = { 0.30f, 0.62f, 0.99f };
    const UIInput& in = ui.input();
    Renderer* r = ui.r;
    g.dropHoverValid = false;
    g.dropTileName.clear();
    if (!g.dragItemActive) g.dropHoverRel.clear();

    // ── editable path (Unreal-style: shows where you are, type to jump) ──
    std::string shown = g.projectName + (g.curRel.empty() ? "" : "\\" + g.curRel);
    bool pathFocused = ui.inputFocused("pathedit");
    if (!pathFocused && g.pathEditSynced != shown) {
        snprintf(g.pathEdit, sizeof(g.pathEdit), "%s", shown.c_str());
        g.pathEditSynced = shown;
    }
    ui.textInput("pathedit", g.pathEdit, sizeof(g.pathEdit));
    if (pathFocused && in.keyEnter) {
        std::string q = g.pathEdit;
        for (auto& c : q) if (c == '/') c = '\\';
        if (q.rfind(g.projectName, 0) == 0) q = q.substr(g.projectName.size());
        while (!q.empty() && q.front() == '\\') q.erase(q.begin());
        while (!q.empty() && (q.back() == '\\' || q.back() == ' ')) q.pop_back();
        std::error_code ec;
        if (fs::is_directory(relAbs(q), ec) || q.empty()) {
            g.curRel = q;
            browserClearSelection();
            g.pathEditSynced.clear();
            scanBrowser();
        } else {
            addLog(2, "Path not found: %s", q.c_str());
        }
    }

    UIRect pin = ui.panelInner();
    float topY = ui.panelCursorY();
    bool ctxOpen = g.browserCtx != 0;

    // Delete key (routed here from WndProc): remove the selected assets from the
    // browser view currently under the cursor, with a single confirmation.
    if (g.browserDeletePending && !ctxOpen && !ui.interactionBlocked() && !ui.wantKeyboard() && !g.renameOpen &&
        in.mouseX >= pin.x && in.mouseX < pin.x + pin.w && in.mouseY >= pin.y && in.mouseY < pin.y + pin.h &&
        !g.browserSelected.empty()) {
        g.browserDeletePending = false;
        browserDeleteSelected();
    }

    // ── draggable splitter between folder column and contents ──
    float splitX = pin.x + g.browserSplit + 2;
    bool overSplit = !ctxOpen && !ui.interactionBlocked() &&
        in.mouseX >= splitX - 3 && in.mouseX <= splitX + 4 &&
        in.mouseY >= topY && in.mouseY < pin.y + pin.h;
    if (overSplit && in.mousePressed) g.browserSplitDrag = true;
    if (g.browserSplitDrag) {
        g.browserSplit = clampf(in.mouseX - pin.x - 2, 110, pin.w > 340 ? pin.w - 230 : 110);
        if (!in.mouseDown) g.browserSplitDrag = false;
    }

    bool prevBlocked = ui.interactionBlocked();
    if (ctxOpen) ui.setInteractionBlocked(true);

    // ── two columns: folder tree | folder contents ──
    ui.beginColumns(g.browserSplit);
    // clip the folder column so long names don't bleed into the contents
    r->setUIScissor(pin.x + 1, pin.y + 1, g.browserSplit + 2, pin.h - 2, true);
    if (ui.button("Refresh")) { scanBrowser(); addLog(0, "Project browser refreshed."); }
    ui.label("FOLDERS", accent);
    if (!g.folders.empty()) drawFolderNode(ui, 0, 0);

    ui.nextColumn();
    ui.reclipPanel();
    float contentTopY = ui.panelCursorY();
    ui.label("CONTENT  (double click: open | Ctrl+wheel: icon size)", accent);
    float zoomWheel = in.wheel;
    if (in.keyCtrl && zoomWheel != 0 && in.mouseX > splitX + 4 && in.mouseX < pin.x + pin.w &&
        in.mouseY >= contentTopY && in.mouseY < pin.y + pin.h) {
        g.browserTileHeight = clampf(g.browserTileHeight + zoomWheel * 8.0f, 64.0f, 144.0f);
        g.browserTileHeightPreference = g.browserTileHeight;
        saveEditorPreferences();
        ui.consumeWheel();
    }

    // build tile list: [icon, name] — iterate copies (actions rescan the lists)
    struct Tile { std::string name; int icon; };
    std::vector<Tile> tiles;
    if (!g.curRel.empty()) tiles.push_back({ "..", 0 });
    for (const auto& d : g.curDirs) tiles.push_back({ d, 0 });
    for (const auto& f : g.curPfbs) tiles.push_back({ f, 2 });
    for (const auto& f : g.curImps) tiles.push_back({ f, 3 });
    for (const auto& f : g.curBps) tiles.push_back({ f, 4 });
    for (const auto& f : g.curBpis) tiles.push_back({ f, 5 });
    for (const auto& f : g.curCurves) tiles.push_back({ f, 6 });
    for (const auto& f : g.curMeshes) tiles.push_back({ f, 7 });
    for (const auto& f : g.curTextures) tiles.push_back({ f, 8 });
    for (const auto& f : g.curAudio) tiles.push_back({ f, 9 });
    for (const auto& f : g.curAudioClasses) tiles.push_back({ f, 10 });
    for (const auto& f : g.curAudioAttenuations) tiles.push_back({ f, 11 });
    for (const auto& f : g.curAudioConcurrencies) tiles.push_back({ f, 12 });
    for (const auto& f : g.curEnums) tiles.push_back({ f, 13 });
    for (const auto& f : g.curAnimations) tiles.push_back({ f, 14 });
    for (const auto& f : g.curAnimators) tiles.push_back({ f, 15 });
    for (const auto& f : g.curMaterials) tiles.push_back({ f, 16 });
    for (const auto& f : g.curWidgets) tiles.push_back({ f, 17 });
    g.browserVisibleOrder.clear();
    for (const Tile& t : tiles)
        if (t.name != "..") g.browserVisibleOrder.push_back(relJoin(g.curRel, t.name));

    if (tiles.empty()) {
        ui.label("(empty folder)", dim);
        ui.label("Right-click: create folder, save prefab, paste.", dim);
    }
    float contentWidth = (std::max)(80.0f, pin.w - g.browserSplit - 24.0f);
    int cols = (std::max)(1, (int)(contentWidth / (g.browserTileHeight + 12.0f)));
    bool tileRmb = false;
    for (size_t i = 0; i < tiles.size(); i += cols) {
        int n = (int)(tiles.size() - i) < cols ? (int)(tiles.size() - i) : cols;
        ui.row(cols); // fixed cols so tiles keep uniform width
        for (int k = 0; k < n; k++) {
            const Tile& t = tiles[i + k];
            std::string id = "tile_" + t.name;
            std::string itemRel = t.name == ".." ? relParent(g.curRel) : relJoin(g.curRel, t.name);
            bool renamingTile = t.name != ".." && g.renameOpen && g.renameRel == itemRel;
            bool selectedTile = t.name != ".." && (g.browserSelected.count(itemRel) != 0 ||
                                (g.browserSelected.empty() && g.browserSel == t.name));
            int res = ui.iconTile(id.c_str(), browserDisplayName(t.name,t.icon), t.icon, selectedTile || renamingTile,
                                  g.browserTileHeight, !renamingTile,
                                  t.icon==0&&t.name!=".."&&g.folderColors.count(itemRel)?&g.folderColors[itemRel]:nullptr,
                                  browserIconImage(t.name,t.icon));
            UIRect tileRc = ui.lastItemRect();
            if (renamingTile) {
                UIRect edit = { tileRc.x + 5.0f, tileRc.y + tileRc.h - 31.0f,
                                (std::max)(40.0f, tileRc.w - 10.0f), 24.0f };
                browserDrawInlineRename(ui, (id + "_rename").c_str(), edit);
                continue;
            }
            if (res & UI::TILE_PRESSED) {
                g.dragItem = t.name;
                g.dragItemIcon = t.icon;
                g.dragItemActive = false;
                g.dragItemX = in.mouseX;
                g.dragItemY = in.mouseY;
            }
            if ((res & UI::TILE_RCLICKED) && t.name != "..") {
                tileRmb = true;
                if (!g.browserSelected.count(itemRel)) browserSetSingleSelectionRel(itemRel);
                else browserSetActiveRel(itemRel);
                g.browserCtx = 1;
                g.ctxName = t.name;
                g.ctxRelPath = relJoin(g.curRel, t.name);
                g.ctxIcon = t.icon;
                g.ctxX = in.mouseX;
                g.ctxY = in.mouseY;
                g.ctxMoveOpen = false;
                g.ctxCreateOpen = false;
            }
            if ((res & UI::TILE_HOVERED) && g.dragItemActive && t.icon == 0 && t.name != g.dragItem) {
                g.dropTileName = t.name;
            }
            if (!g.dragItemActive) {
                if ((res & 3) == UI::TILE_CLICK) {
                    if (t.name != "..") browserSelectVisibleRel(itemRel, in);
                    else if (!in.keyCtrl && !in.keyShift) browserClearSelection();
                }
                if ((res & 3) == UI::TILE_DBLCLICK) {
                    browserClearSelection();
                    browserOpen(t.name, t.icon);
                }
            }
        }
        // fill dummy cells so the row keeps its height
        for (int k = n; k < cols; k++) ui.spacing(g.browserTileHeight);
    }
    ui.endColumns();

    // splitter highlight over everything in the panel
    if (overSplit || g.browserSplitDrag) {
        r->drawRectPx(splitX - 1, topY, 3, pin.y + pin.h - topY, accent, 0.8f);
    }

    // ── right click on empty content area ──
    if (in.rmbReleased && !tileRmb && !ctxOpen &&
        in.mouseX > splitX + 4 && in.mouseX < pin.x + pin.w &&
        in.mouseY >= contentTopY && in.mouseY < pin.y + pin.h) {
        g.browserCtx = 2;
        g.ctxX = in.mouseX;
        g.ctxY = in.mouseY;
        g.ctxMoveOpen = false;
        g.ctxCreateOpen = false;
    }

    // ── drag & drop ──
    if (!g.dragItem.empty() && in.mouseDown && !g.dragItemActive &&
        (fabsf(in.mouseX - g.dragItemX) > 7 || fabsf(in.mouseY - g.dragItemY) > 7)) {
        g.dragItemActive = true;
    }
    if (in.mouseReleased && !g.dragItem.empty()) {
        if (g.dragItemActive) {
            // Blueprint, clips and reusable audio settings can be assigned directly to the selected
            // object's component by dropping them on Details.
            if (g.dragItemIcon==2 && mouseInViewport() &&
                !(in.mouseX>=pin.x&&in.mouseX<pin.x+pin.w&&in.mouseY>=pin.y&&in.mouseY<pin.y+pin.h) && !g.prefabEditMode) {
                instantiatePrefabAt(g.dragItem,prefabDropPosition());
            } else if ((g.dragItemIcon == 4 || g.dragItemIcon == 7 || (g.dragItemIcon >= 9 && g.dragItemIcon <= 12) ||
                        g.dragItemIcon == 15 || g.dragItemIcon == 16) &&
                g.dock.windowHovered("dettagli", in.mouseX, in.mouseY)) {
                Entity* sel = g.scene.byId(g.selectedId);
                if (!sel) {
                    addLog(2, "Select an object in the viewport or the Outliner first.");
                } else {
                    std::string rel = relJoin(g.curRel, g.dragItem);
                    if (g.dragItemIcon == 4) {
                        bool added = addBlueprintComponent(*sel, rel);
                        addLog(added ? 1 : 2, added ? "Component %s added to %s."
                                                   : "Component %s is single-instance and is already on %s.",
                               fs::path(rel).stem().string().c_str(), sel->name);
                    } else if (g.dragItemIcon == 9) {
                        g.audio.stop(sel->id);
                        sel->hasAudio = true;
                        snprintf(sel->audioClip, sizeof(sel->audioClip), "%s", rel.c_str());
                        addLog(1, "Audio '%s' assigned to the Audio Source of %s.", rel.c_str(), sel->name);
                    } else if (g.dragItemIcon == 10) {
                        sel->hasAudio = true;
                        snprintf(sel->audioClass, sizeof(sel->audioClass), "%s", rel.c_str());
                        addLog(1, "Audio Class '%s' assigned to %s.", rel.c_str(), sel->name);
                    } else if (g.dragItemIcon == 11) {
                        sel->hasAudio = true;
                        snprintf(sel->audioAttenuation, sizeof(sel->audioAttenuation), "%s", rel.c_str());
                        addLog(1, "Attenuation '%s' assigned to %s.", rel.c_str(), sel->name);
                    } else if (g.dragItemIcon == 12) {
                        sel->hasAudio = true;
                        snprintf(sel->audioConcurrency, sizeof(sel->audioConcurrency), "%s", rel.c_str());
                        addLog(1, "Concurrency '%s' assegnata a %s.", rel.c_str(), sel->name);
                    } else if (g.dragItemIcon == 7) {          // mesh model → Mesh Renderer
                        sel->hasMesh = true;
                        sel->mesh = MESH_CUBE;                 // primitive collision fallback
                        snprintf(sel->meshAsset, sizeof(sel->meshAsset), "%s", rel.c_str());
                        g.scene.syncBodyShape(*sel);
                        addLog(1, "Mesh '%s' assegnata a %s.", rel.c_str(), sel->name);
                    } else if (g.dragItemIcon == 16) {         // material → Mesh Renderer
                        snprintf(sel->materialAsset, sizeof(sel->materialAsset), "%s", rel.c_str());
                        addLog(1, "Material '%s' assigned to %s.", rel.c_str(), sel->name);
                    } else {
                        sel->hasAnimator=true;
                        snprintf(sel->animatorController,sizeof(sel->animatorController),"%s",rel.c_str());
                        sel->animatorRuntimeState=0;sel->animatorRuntimeTime=0;sel->animatorRuntimePlaying=false;
                        addLog(1,"Animator Controller '%s' assigned to %s.",rel.c_str(),sel->name);
                    }
                }
            } else if (g.dropHoverValid) {
                browserMove(g.dragItem, g.dropHoverRel);
            } else if (g.dropTileName == "..") {
                browserMove(g.dragItem, relParent(g.curRel));
            } else if (!g.dropTileName.empty()) {
                browserMove(g.dragItem, relJoin(g.curRel, g.dropTileName));
            }
        }
        g.dragItem.clear();
        g.dragItemActive = false;
    }
    if (g.dragItemActive) {
        // ghost follows the cursor; drawn unclipped so it shows over other panels
        bool assetTarget = (g.dragItemIcon == 4 || g.dragItemIcon == 7 || (g.dragItemIcon >= 9 && g.dragItemIcon <= 12) ||
                            g.dragItemIcon == 15 || g.dragItemIcon == 16) &&
                           g.dock.windowHovered("dettagli", in.mouseX, in.mouseY);
        r->setUIScissor(0, 0, 0, 0, false);
        std::string dragLabel=browserDisplayName(g.dragItem,g.dragItemIcon);
        float tw = r->textWidth(dragLabel);
        r->drawRectPx(in.mouseX + 12, in.mouseY + 8, tw + 14, 19,
                      assetTarget ? Vec3{ 0.13f, 0.30f, 0.50f } : Vec3{ 0.1f, 0.11f, 0.13f }, 0.95f);
        r->drawTextLine(in.mouseX + 19, in.mouseY + 10, dragLabel, { 0.85f, 0.9f, 1.0f }, 1);
        if (assetTarget) r->drawTextLine(in.mouseX + 19, in.mouseY + 28, "assign to selection", { 0.7f, 0.85f, 1.0f }, 1);
        ui.reclipPanel();
    }

    ui.setInteractionBlocked(prevBlocked);

    // ── context menu (drawn last, on top) ──
    if (g.browserCtx != 0) {
        struct MItem { const char* label; int action; };
        std::vector<MItem> items;
        if (g.browserCtx == 1) {
            items.push_back({ g.ctxIcon == 2 ? "Instantiate" : "Open", 1 });
            if (g.ctxIcon == 4) items.push_back({ "Create Child Blueprint", 8 });
            if (g.ctxIcon == 0) items.push_back({ "Folder color...", 16 });
            items.push_back({ "Rename", 7 });   // every asset and folder can be renamed
            items.push_back({ "Copy", 2 });
            items.push_back({ "Duplicate", 3 });
            items.push_back({ "Move to...", 4 });
            items.push_back({ "Delete", 5 });
        } else if (g.browserCtx == 3) {
            items.push_back({ "Open", 14 });
            items.push_back({ "Folder color...", 16 });
            if (!g.ctxRelPath.empty()) items.push_back({ "Rename", 7 });
        } else {
            items.push_back({ "Import assets...", 15 });
            items.push_back({ "Create asset", 9 });
            items.push_back({ "Create folder", 10 });
            items.push_back({ "Save prefab here...", 11 });
            if (!g.fileClipboard.empty()) items.push_back({ "Paste", 12 });
            items.push_back({ "Refresh", 13 });
        }
        const float MW = 210, IH = 22;
        // header with the target asset/folder name (long names are ellipsized and
        // shown in full on hover, like the rest of the editor)
        std::string ctxTitle;
        if (g.browserCtx == 1) ctxTitle = browserDisplayName(g.ctxName, g.ctxIcon);
        else if (g.browserCtx == 3 && !g.ctxRelPath.empty()) ctxTitle = fs::path(g.ctxRelPath).filename().string();
        float headH = ctxTitle.empty() ? 0.0f : (IH + 2);
        float mh = (float)items.size() * IH + headH + 8;
        float mx = clampf(g.ctxX, pin.x, pin.x + pin.w - MW - 190);
        float my = clampf(g.ctxY, pin.y, pin.y + pin.h - mh - 4);
        r->drawRectPx(mx + 3, my + 4, MW, mh, { 0, 0, 0 }, 0.35f);
        r->drawRectPx(mx, my, MW, mh, { 0.13f, 0.145f, 0.17f }, 0.99f);
        if (!ctxTitle.empty()) {
            UIRect hrc = { mx, my + 4, MW, IH };
            r->drawTextLine(mx + 12, my + 7, ui.ellipsize(ctxTitle, MW - 20.0f), { 0.62f, 0.72f, 0.85f }, 1);
            ui.hoverTip(ctxTitle, hrc, MW - 20.0f);
            r->drawRectPx(mx + 6, my + 4 + IH, MW - 12, 1, { 0.28f, 0.30f, 0.34f }, 1);
        }
        float itemsTop = my + 4 + headH;
        int hoverIdx = -1, action = -1;
        for (int i = 0; i < (int)items.size(); i++) {
            float iy = itemsTop + i * IH;
            bool hov = in.mouseX >= mx && in.mouseX < mx + MW && in.mouseY >= iy && in.mouseY < iy + IH;
            if (hov) hoverIdx = i;
            bool subHi = (items[i].action == 4 && g.ctxMoveOpen) || (items[i].action == 9 && g.ctxCreateOpen);
            if (hov || subHi) r->drawRectPx(mx + 2, iy, MW - 4, IH, { 0.2f, 0.32f, 0.5f }, 1);
            bool hasArrow = items[i].action == 4 || items[i].action == 9;
            Vec3 labelCol = items[i].action == 5 ? Vec3{ 1.0f, 0.45f, 0.42f } : Vec3{ 0.87f, 0.9f, 0.95f };
            float labelMax = MW - 12.0f - (hasArrow ? 18.0f : 8.0f);
            UIRect irc = { mx, iy, MW, IH };
            r->drawTextLine(mx + 12, iy + 3, ui.ellipsize(items[i].label, labelMax), labelCol, 1);
            ui.hoverTip(items[i].label, irc, labelMax);
            if (hasArrow) r->drawTextLine(mx + MW - 16, iy + 3, ">", accent, 1);
            if (hov && in.mousePressed) action = items[i].action;
        }
        if (hoverIdx >= 0 && items[hoverIdx].action == 4) { g.ctxMoveOpen = true; g.ctxCreateOpen = false; }
        else if (hoverIdx >= 0 && items[hoverIdx].action == 9) { g.ctxCreateOpen = true; g.ctxMoveOpen = false; }
        else if (hoverIdx >= 0) { g.ctxMoveOpen = false; g.ctxCreateOpen = false; }

        // "Move to..." submenu: pick the destination folder
        bool inSub = false;
        if (g.ctxMoveOpen && g.browserCtx == 1) {
            std::string srcRel = relJoin(g.curRel, g.ctxName);
            std::vector<int> opts;
            for (int fi = 0; fi < (int)g.folders.size(); fi++) {
                const std::string& fr = g.folders[fi].rel;
                if (g.ctxIcon == 0 && (fr == srcRel || fr.rfind(srcRel + "\\", 0) == 0)) continue;
                if (fr == g.curRel) continue;
                opts.push_back(fi);
            }
            float sh = (float)opts.size() * IH + 8;
            float maxH = 300;
            if (sh > maxH) sh = maxH;
            float sx = mx + MW + 2;
            float sy = clampf(my + 4 + headH, pin.y, pin.y + pin.h - sh - 4);
            r->drawRectPx(sx + 3, sy + 4, 176, sh, { 0, 0, 0 }, 0.35f);
            r->drawRectPx(sx, sy, 176, sh, { 0.13f, 0.145f, 0.17f }, 0.99f);
            inSub = in.mouseX >= sx && in.mouseX < sx + 176 && in.mouseY >= sy && in.mouseY < sy + sh;
            if (inSub && in.wheel != 0) g.ctxMoveScroll += in.wheel * 30;
            float maxScroll = (float)opts.size() * IH + 8 - sh;
            g.ctxMoveScroll = clampf(g.ctxMoveScroll, -(maxScroll > 0 ? maxScroll : 0), 0);
            for (int oi = 0; oi < (int)opts.size(); oi++) {
                float iy = sy + 4 + oi * IH + g.ctxMoveScroll;
                if (iy < sy - IH || iy > sy + sh) continue;
                const auto& node = g.folders[opts[oi]];
                std::string lbl = node.rel.empty() ? g.projectName : node.rel;
                while (lbl.size() > 3 && r->textWidth(lbl) > 150) lbl = lbl.substr(1);
                bool hov = in.mouseX >= sx && in.mouseX < sx + 176 && in.mouseY >= iy && in.mouseY < iy + IH;
                if (hov) r->drawRectPx(sx + 2, iy, 172, IH, { 0.2f, 0.32f, 0.5f }, 1);
                r->drawTextLine(sx + 12, iy + 3, lbl, { 0.87f, 0.9f, 0.95f }, 1);
                if (hov && in.mousePressed) {
                    browserMove(g.ctxName, node.rel);
                    g.browserCtx = 0;
                    action = 0;
                }
            }
        }

        // Unreal-style asset creation submenu.
        if (g.ctxCreateOpen && g.browserCtx == 2) {
            static const MItem createItems[] = {
                { "Blueprint Actor", 20 },
                { "Blueprint GameMode", 27 },
                { "Blueprint GameInstance", 28 },
                { "Blueprint PlayerController", 29 },
                { "Blueprint SaveGame", 30 },
                { "Blueprint Interface", 21 },
                { "Curve", 22 },
                { "Audio Class", 23 },
                { "Audio Attenuation", 24 },
                { "Audio Concurrency", 25 },
                { "Enum", 26 },
                { "Animation Clip", 31 },
                { "Animator Controller", 32 },
                { "Material", 33 },
                { "Widget UI", 34 },
            };
            const int createCount = (int)(sizeof(createItems) / sizeof(createItems[0]));
            const float SW = 184, sh = createCount * IH + 8;
            float sx = mx + MW + 2;
            float sy = clampf(my + 4 + headH, pin.y, pin.y + pin.h - sh - 4);
            r->drawRectPx(sx + 3, sy + 4, SW, sh, { 0, 0, 0 }, 0.35f);
            r->drawRectPx(sx, sy, SW, sh, { 0.13f, 0.145f, 0.17f }, 0.99f);
            inSub = inSub || (in.mouseX >= sx && in.mouseX < sx + SW && in.mouseY >= sy && in.mouseY < sy + sh);
            for (int i = 0; i < createCount; i++) {
                float iy = sy + 4 + i * IH;
                bool hov = in.mouseX >= sx && in.mouseX < sx + SW && in.mouseY >= iy && in.mouseY < iy + IH;
                if (hov) r->drawRectPx(sx + 2, iy, SW - 4, IH, { 0.2f, 0.32f, 0.5f }, 1);
                r->drawTextLine(sx + 12, iy + 3, createItems[i].label, { 0.87f, 0.9f, 0.95f }, 1);
                if (hov && in.mousePressed) action = createItems[i].action;
            }
        }

        bool inMain = in.mouseX >= mx && in.mouseX < mx + MW && in.mouseY >= my && in.mouseY < my + mh;
        switch (action) {
        case 1:
            if(g.ctxIcon==2)instantiatePrefab(g.ctxName);
            else browserOpen(g.ctxName, g.ctxIcon);
            break;
        case 2:
            g.fileClipboard = curDirAbs() + "\\" + g.ctxName;
            addLog(0, "Copied: %s", g.ctxName.c_str());
            break;
        case 3: browserDuplicate(g.ctxName); break;
        case 5: browserDelete(g.ctxName); break;
        case 7:
            browserStartRename(g.browserCtx == 3 ? g.ctxRelPath : relJoin(g.curRel, g.ctxName));
            break;
        case 8: browserCreateChildBlueprint(g.ctxRelPath); break;
        case 10: {
            std::error_code ec;
            std::string nm = uniqueDest(curDirAbs(), "NewFolder");
            fs::create_directories(curDirAbs() + "\\" + nm, ec);
            addLog(ec ? 2 : 1, ec ? "Could not create the folder." : "Folder created: %s", nm.c_str());
            scanBrowser();
            if (!ec) {
                std::string rel = relJoin(g.curRel, nm);
                browserSetSingleSelectionRel(rel);
                browserStartRename(rel);
            }
            break;
        }
        case 11: savePrefab(); break;
        case 12: browserPaste(); break;
        case 13: scanBrowser(); addLog(0, "Project browser refreshed."); break;
        case 14:
            g.curRel = g.ctxRelPath;
            browserClearSelection();
            scanBrowser();
            break;
        case 15: browserImportAssets(); break;
        case 16: {
            std::string rel=g.browserCtx==3?g.ctxRelPath:relJoin(g.curRel,g.ctxName);
            if(!g.folderColors.count(rel))g.folderColors[rel]={.86f,.66f,.31f};
            ui.openColorPicker(("folder_"+rel).c_str(),&g.folderColors[rel],nullptr,g.ctxX,g.ctxY);
            saveEditorPreferences();
            break;
        }
        case 20: browserCreateAsset(0); break;
        case 21: browserCreateAsset(1); break;
        case 22: browserCreateAsset(2); break;
        case 23: browserCreateAsset(3); break;
        case 24: browserCreateAsset(4); break;
        case 25: browserCreateAsset(5); break;
        case 26: browserCreateAsset(6); break;
        case 27: browserCreateAsset(7); break;
        case 28: browserCreateAsset(8); break;
        case 29: browserCreateAsset(9); break;
        case 30: browserCreateAsset(10); break;
        case 31: browserCreateAsset(11); break;
        case 32: browserCreateAsset(12); break;
        case 33: browserCreateAsset(13); break;
        case 34: browserCreateAsset(14); break;
        }
        if (action > 0 && action != 4 && action != 9) g.browserCtx = 0;
        if (in.keyEscape || (in.mousePressed && !inMain && !inSub)) g.browserCtx = 0;
        if (g.browserCtx == 0) g.ctxMoveScroll = 0;
    }
}

static void collectBlueprintClasses(BPClassKind kind, std::vector<std::string>& paths, std::vector<std::string>& labels) {
    paths = { "" };
    labels = { "None" };
    std::error_code ec;
    for (fs::recursive_directory_iterator it(g.projectDir, fs::directory_options::skip_permission_denied, ec), end;
         !ec && it != end; it.increment(ec)) {
        if (!it->is_regular_file(ec) || _stricmp(it->path().extension().string().c_str(), ".bp") != 0) continue;
        std::string data;
        BPGraph graph;
        if (!readFile(it->path().string(), data) || !graph.deserialize(data) || graph.classKind != kind) continue;
        std::string rel = fs::relative(it->path(), g.projectDir, ec).string();
        if (ec) { ec.clear(); continue; }
        paths.push_back(rel);
        labels.push_back(it->path().stem().string() + "  (" + rel + ")");
    }
}

// list every .imp scene under the project (relative paths), for the startup combo
static void collectLevelScenes(std::vector<std::string>& rels) {
    rels.clear();
    std::error_code ec;
    for (fs::recursive_directory_iterator it(g.projectDir, fs::directory_options::skip_permission_denied, ec), end;
         !ec && it != end; it.increment(ec)) {
        if (!it->is_regular_file(ec) || _stricmp(it->path().extension().string().c_str(), ".imp") != 0) continue;
        std::string rel = fs::relative(it->path(), g.projectDir, ec).string();
        if (!ec) rels.push_back(rel);
    }
}

// ── Project Settings modal: category pages (File > Impostazioni progetto) ──
static void drawSettingsGenerali(UI& ui) {
    ui.header("STARTUP");
    std::vector<std::string> levels; collectLevelScenes(levels);
    std::vector<std::string> lvlLabels = { "Last opened / Main" };
    for (const std::string& rel : levels) lvlLabels.push_back(rel);
    std::vector<const char*> lvlPtrs; for (auto& l : lvlLabels) lvlPtrs.push_back(l.c_str());
    int startPick = 0;
    for (int i = 0; i < (int)levels.size(); i++) if (_stricmp(levels[i].c_str(), g.startupLevel.c_str()) == 0) startPick = i + 1;
    if (ui.combo("Startup scene (when the project opens)", &startPick, lvlPtrs.data(), (int)lvlPtrs.size())) {
        g.startupLevel = startPick <= 0 ? std::string() : levels[startPick - 1];
        saveGameplayProjectSettings();
    }

    ui.header("GAMEPLAY FRAMEWORK");
    std::vector<std::string> paths, labels;
    std::vector<const char*> labelPtrs;
    collectBlueprintClasses(BP_CLASS_GAMEMODE, paths, labels);
    for (const std::string& label : labels) labelPtrs.push_back(label.c_str());
    int defModePick = 0;
    for (int i = 1; i < (int)paths.size(); i++) if (_stricmp(paths[i].c_str(), g.defaultGameModeAsset.c_str()) == 0) defModePick = i;
    if (ui.combo("Base GameMode (all levels)", &defModePick, labelPtrs.data(), (int)labelPtrs.size())) {
        g.defaultGameModeAsset = paths[defModePick];
        saveGameplayProjectSettings();
    }
    int gameModePick = 0;
    for (int i = 1; i < (int)paths.size(); i++) if (_stricmp(paths[i].c_str(), g.scene.gameModePath.c_str()) == 0) gameModePick = i;
    if (ui.combo("GameMode for this level", &gameModePick, labelPtrs.data(), (int)labelPtrs.size()))
        g.scene.gameModePath = paths[gameModePick];

    collectBlueprintClasses(BP_CLASS_GAMEINSTANCE, paths, labels);
    labelPtrs.clear();
    for (const std::string& label : labels) labelPtrs.push_back(label.c_str());
    int instancePick = 0;
    for (int i = 1; i < (int)paths.size(); i++) if (_stricmp(paths[i].c_str(), g.gameInstanceAsset.c_str()) == 0) instancePick = i;
    if (ui.combo("Project GameInstance", &instancePick, labelPtrs.data(), (int)labelPtrs.size())) {
        g.gameInstanceAsset = paths[instancePick];
        g.persistentGameInstanceVars.clear();
        saveGameplayProjectSettings();
    }
    ui.label("The base GameMode applies to levels without their own.", { .55f, .59f, .66f });
    ui.label("The GameInstance persists for the session; the GameMode lives in the level.", { .55f, .59f, .66f });

    std::vector<std::string> hudLabels = { "None" };
    for (const std::string& w : g.projectWidgetAssets) { fs::path p(w); p.replace_extension(); hudLabels.push_back(p.string()); }
    std::vector<const char*> hudPtrs; for (auto& l : hudLabels) hudPtrs.push_back(l.c_str());
    int hudPick = 0;
    for (int i = 0; i < (int)g.projectWidgetAssets.size(); i++) if (_stricmp(g.projectWidgetAssets[i].c_str(), g.scene.hudWidget.c_str()) == 0) hudPick = i + 1;
    if (ui.combo("HUD Widget (in Play)", &hudPick, hudPtrs.data(), (int)hudPtrs.size()))
        g.scene.hudWidget = hudPick <= 0 ? std::string() : g.projectWidgetAssets[hudPick - 1];
}

static void drawSettingsFisica(UI& ui) {
    ui.header("MONDO");
    float grav = g.scene.gravityY;
    if (ui.dragFloat("Gravity Y", &grav, 0.05f, -100, 100)) {
        g.scene.gravityY = grav;
        g.scene.world.gravity = { 0, grav, 0 };
    }
    ui.checkbox("Show contact points", &g.showContacts);
    // ── matrice collisioni (Unity-style: quali layer collidono tra loro) ──
    ui.header("COLLISIONS (LAYERS)");
    CollisionLayers& L = g.scene.layers;
    ui.label("Nomi dei layer:", { 0.55f, 0.59f, 0.66f });
    for (int i = 0; i < L.count; i++) {
        char lid[24];
        snprintf(lid, sizeof(lid), "layname%d", i);
        if (ui.textInput(lid, L.names[i], sizeof(L.names[i]))) {
            for (char* c = L.names[i]; *c; c++) if (*c == ' ') *c = '_';
        }
    }
    if (L.count < CollisionLayers::MAX && ui.button("+ Add layer")) {
        snprintf(L.names[L.count], sizeof(L.names[L.count]), "Layer%d", L.count);
        for (int j = 0; j < CollisionLayers::MAX; j++) { L.matrix[L.count][j] = true; L.matrix[j][L.count] = true; }
        L.count++;
        g.scene.applyLayersToWorld();
    }
    ui.spacing(2);
    ui.label("Who collides with whom:", { 0.55f, 0.59f, 0.66f });
    for (int i = 0; i < L.count; i++) {
        for (int j = i; j < L.count; j++) {
            char lbl[80];
            snprintf(lbl, sizeof(lbl), "%s  x  %s##m%d_%d", L.names[i], L.names[j], i, j);
            bool v = L.matrix[i][j];
            if (ui.checkbox(lbl, &v)) {
                L.matrix[i][j] = v;
                L.matrix[j][i] = v;
                g.scene.applyLayersToWorld();
            }
        }
    }
}

static void drawSettingsRendering(UI& ui) {
    ui.header("LIGHTING");
    ui.dragFloat("Sun: azimuth", &g.sunAzimuth, 0.5f, -360, 360);
    ui.dragFloat("Sun: elevation", &g.sunElevation, 0.3f, 5, 89);
    ui.dragFloat("Sole: forza", &g.sunIntensity, 0.01f, 0, 4);
    ui.dragFloat("Ombre", &g.shadowStrength, 0.01f, 0, 1);
    ui.dragFloat("Fog", &g.fogDensity, 0.0002f, 0, 0.05f);
    ui.header("VIDEO");
    bool vs = g.vsync;
    if (ui.checkbox("VSync", &vs)) {
        g.vsync = vs;
        if (wglSwapIntervalEXT) wglSwapIntervalEXT(vs ? 1 : 0);
    }
}

static void drawSettingsLingua(UI& ui) {
    ui.header("LANGUAGE");
    // Only English ships for now; more entries go here once their strings exist.
    static const char* langs[] = { "English" };
    const int langCount = (int)(sizeof(langs) / sizeof(langs[0]));
    int pick = 0;
    for (int i = 0; i < langCount; i++) if (g.language == langs[i]) pick = i;
    if (ui.combo("Editor language", &pick, langs, langCount)) {
        g.language = langs[pick];
        saveGameplayProjectSettings();
    }
    ui.label("The editor UI is in English. More languages will appear here.", { .55f, .59f, .66f });
}

static UIRect settingsWindowRect() {
    float w = clampf((float)g.width - 120.0f, 620.0f, 900.0f);
    float h = clampf((float)g.height - TOP_H - 80.0f, 460.0f, 720.0f);
    return { ((float)g.width - w) * 0.5f, TOP_H + 28.0f, w, h };
}

static void drawSettingsWindow() {
    if (!g.settingsWindowOpen) return;
    UI& ui = g.ui;
    UIRect rc = settingsWindowRect();
    ui.panelBegin("project_settings", rc.x, rc.y, rc.w, rc.h, "PROJECT SETTINGS");
    static const char* cats[] = { "General", "Physics", "Rendering", "Language" };
    ui.tabBar(cats, 4, &g.settingsCategory);
    ui.spacing(6);
    switch (g.settingsCategory) {
    case 0: drawSettingsGenerali(ui); break;
    case 1: drawSettingsFisica(ui); break;
    case 2: drawSettingsRendering(ui); break;
    case 3: drawSettingsLingua(ui); break;
    }
    ui.spacing(8);
    if (ui.button("Close")) g.settingsWindowOpen = false;
    ui.panelEnd();
}

static void drawNavigationContent(UI& ui) {
    ui.header("NAVIGATION");
    ui.label("Bake a navigable grid from the marked surfaces", { .72f, .80f, .90f });
    ui.label("Static > Navigation in the Inspector.", { .72f, .80f, .90f });
    int sources = 0, agents = 0, occluders = 0;
    for (const Entity& e : g.scene.entities) {
        if ((e.staticFlags & STATIC_NAVIGATION) && e.hasMesh) sources++;
        if (e.hasAIAgent) agents++;
        if (e.hasNavigationOccluder) occluders++;
    }
    char counts[128]; snprintf(counts, sizeof(counts), "Surfaces: %d   |   Occluders: %d   |   AI Agents: %d", sources, occluders, agents);
    ui.label(counts, { .50f, .76f, 1.0f });
    ui.spacing(5);
    ui.dragFloat("Cell size", &g.navigation.cellSize, 0.02f, 0.1f, 5.0f);
    ui.dragFloat("Max step height", &g.navigation.stepHeight, 0.02f, 0.0f, 5.0f);
    ui.dragFloat("Distance from edges (agent radius)", &g.navigation.agentRadius, 0.01f, 0.0f, 10.0f);
    if(ui.checkbox("Show NavMesh and path (N)", &g.navigation.show))saveEditorPreferences();
    ui.row(2);
    if (ui.buttonColored("BAKE NAVIGATION", { .08f, .32f, .22f }, { .65f, 1.0f, .78f })) bakeNavigation();
    if (ui.button("Clear Bake")) {
        g.navigation.cells.clear(); g.navigation.baked = false;
        g.navigation.status = "NavMesh cleared.";
        for (Entity& e : g.scene.entities) if (e.hasAIAgent) { e.aiHasPath = false; e.aiPath.clear(); }
    }
    ui.spacing(5);
    ui.labelWrapped(g.navigation.status, g.navigation.baked ? Vec3{ .50f, .95f, .64f } : Vec3{ .92f, .62f, .36f });
    if (g.navigation.baked) {
        char mesh[128]; snprintf(mesh, sizeof(mesh), "Cells: %dx%d | surfaces: %d | occluders: %d", g.navigation.width, g.navigation.height, g.navigation.sourceCount, g.navigation.occluderCount);
        ui.label(mesh, { .55f, .59f, .66f });
    }
    ui.spacing(7);
    ui.header("BLUEPRINT USAGE");
    ui.label("AI Set Target / AI Set Destination", { .76f, .82f, .92f });
    ui.label("AI Set Speed / AI Set Is Stopped", { .76f, .82f, .92f });
    ui.label("AI Remaining Distance / AI Has Path", { .76f, .82f, .92f });
}

static void collectAnimationClips(std::vector<std::string>& paths, std::vector<std::string>& labels) {
    paths = { "" }; labels = { "None" };
    std::error_code ec;
    for (fs::recursive_directory_iterator it(g.projectDir, fs::directory_options::skip_permission_denied, ec), end;
         !ec && it != end; it.increment(ec)) {
        if (!it->is_regular_file(ec) || _stricmp(it->path().extension().string().c_str(), ".anim") != 0) continue;
        std::string rel = fs::relative(it->path(), g.projectDir, ec).string();
        if (ec) { ec.clear(); continue; }
        paths.push_back(rel); labels.push_back(it->path().stem().string() + "  (" + rel + ")");
    }
}

static AnimationKey selectedAnimationKey() {
    AnimationKey k; k.time = g.animationTime;
    Entity* e = g.scene.byId(g.selectedId);
    if (e && e->body) {
        k.entityId = e->id;
        k.objectName = e->name;
        Entity* parent=e->parentId?g.scene.byId(e->parentId):nullptr;
        bool useLocal=parent&&parent->body;
        // Keep an existing v1/v2 world-space track internally consistent when
        // adding more keys; newly-created parented tracks use local space.
        for(const AnimationKey& existing:g.animationEdit.keys)
            if((existing.entityId&&existing.entityId==e->id)||(!existing.entityId&&existing.objectName==e->name)){
                useLocal=existing.localSpace;break;
            }
        k.localSpace=useLocal;
        if(useLocal&&parent&&parent->body){
            k.position=divComponents(parent->body->quat.conjugate().rotate(e->body->position-parent->body->position),parent->scale);
            k.rotation=(parent->body->quat.conjugate()*e->body->quat).normalized();
            k.scale=divComponents(e->scale,parent->scale);
        }else{
            k.position=e->body->position;k.rotation=e->body->quat;k.scale=e->scale;
        }
    }
    return k;
}

static void addOrReplaceAnimationKey() {
    Entity* e = g.scene.byId(g.selectedId);
    if (!e || !e->body) { addLog(2, "Select an object to animate."); return; }
    AnimationKey k = selectedAnimationKey();
    int replace = -1;
    for (int i = 0; i < (int)g.animationEdit.keys.size(); i++)
        if (g.animationEdit.keys[i].entityId == k.entityId &&
            fabsf(g.animationEdit.keys[i].time - k.time) < .002f) replace = i;
    if (replace >= 0) g.animationEdit.keys[replace] = k;
    else g.animationEdit.keys.push_back(k);
    g.animationEdit.sortKeys();
    g.animationSelectedKeys.clear();
    for (int i = 0; i < (int)g.animationEdit.keys.size(); i++)
        if (g.animationEdit.keys[i].entityId == k.entityId && fabsf(g.animationEdit.keys[i].time-k.time)<.002f) {
            g.animationSelectedKey=i;
            g.animationSelectedKeys.push_back(i);
        }
    g.animationObservedEntity = e->id; g.animationObservedPos = k.position;
    g.animationObservedRot = k.rotation; g.animationObservedScale = k.scale;
}

static bool saveAnimationClip() {
    std::string targetRel = g.animationEditRel;
    bool creating = targetRel.empty();
    if (creating) {
        char selected[MAX_PATH] = "";
        if (!fileDialog(true, "Pulse Engine Animation Clip (*.anim)\0*.anim\0All files (*.*)\0*.*\0",
                        "anim", selected, MAX_PATH, "NewAnimation.anim", g.projectDir.c_str())) return false;
        std::error_code ec;
        fs::path relative = fs::relative(fs::path(selected), fs::path(g.projectDir), ec).lexically_normal();
        std::string rel = relative.string();
        if (ec || rel.empty() || rel == ".." || rel.rfind("..\\", 0) == 0 || rel.rfind("../", 0) == 0) {
            addLog(2, "The Animation Clip must be saved inside the project folder.");
            return false;
        }
        targetRel = rel;
    }
    bool ok = writeFile(relAbs(targetRel), g.animationEdit.serialize());
    if (ok && creating) {
        g.animationEditRel = targetRel;
        scanBrowser();
        browserSetSingleSelectionRel(targetRel);
    }
    addLog(ok ? 1 : 2, ok ? (creating ? "Animation Clip created and saved: %s" : "Animation Clip saved: %s")
                           : "Saving the Animation Clip failed: %s", targetRel.c_str());
    return ok;
}

static Entity* animationTrackEntity(int entityId, const std::string& objectName) {
    Entity* e = entityId ? g.scene.byId(entityId) : nullptr;
    if (!e && !objectName.empty())
        for (Entity& candidate : g.scene.entities)
            if (objectName == candidate.name) { e = &candidate; break; }
    return e;
}

static void captureAnimationPreviewObjects() {
    std::vector<int> affected;
    for (const AnimationKey& key : g.animationEdit.keys) {
        Entity* root = animationTrackEntity(key.entityId, key.objectName);
        if (!root) continue;
        std::vector<int> subtree;
        g.scene.collectSubtree(root->id, subtree);
        for (int id : subtree)
            if (std::find(affected.begin(), affected.end(), id) == affected.end()) affected.push_back(id);
    }
    // REC puo' iniziare prima che l'oggetto selezionato abbia una traccia.
    if (g.animationRecording && g.selectedId) {
        std::vector<int> subtree;
        g.scene.collectSubtree(g.selectedId, subtree);
        for (int id : subtree)
            if (std::find(affected.begin(), affected.end(), id) == affected.end()) affected.push_back(id);
    }
    for (int id : affected) {
        bool stored = false;
        for (const auto& original : g.animationPreviewOriginal)
            if (original.entityId == id) { stored = true; break; }
        Entity* e = g.scene.byId(id);
        if (!stored && e && e->body)
            g.animationPreviewOriginal.push_back({id,e->body->position,e->body->quat,e->scale});
    }
    if (!g.animationPreviewOriginal.empty()) g.animationPreviewActive = true;
}

static void restoreAnimationPreview() {
    if (!g.animationPreviewActive) return;
    g.sceneHistorySkipFrame = true;
    for (const auto& original : g.animationPreviewOriginal) {
        Entity* e = g.scene.byId(original.entityId);
        if (!e || !e->body) continue;
        e->body->position = original.position;
        e->body->quat = original.rotation;
        e->scale = original.scale;
        e->body->velocity = {};
        e->body->angularVelocity = {};
        e->body->updateInertiaWorld();
        g.scene.syncBodyShape(*e);
    }
    g.animationPreviewOriginal.clear();
    g.animationPreviewActive = false;
}

static void applyAnimationPreview(float time) {
    if (g.animationEdit.keys.empty()) return;
    // Previewing/scrubbing an animation must not consume scene undo slots.
    g.sceneHistorySkipFrame = true;
    captureAnimationPreviewObjects();
    std::vector<std::pair<int,std::string>> tracks;
    for (const AnimationKey& source : g.animationEdit.keys) {
        std::pair<int,std::string> id{source.entityId, source.objectName};
        if (std::find(tracks.begin(), tracks.end(), id) == tracks.end()) tracks.push_back(id);
    }
    for (const auto& track : tracks) {
        Entity* e = animationTrackEntity(track.first, track.second);
        if (!e || !e->body) continue;
        // Scrubbing in editor is clamped even for looping clips: beyond the
        // final key the preview must hold the final pose. Runtime playback
        // still uses the clip's loop setting through the default argument.
        AnimationKey k = g.animationEdit.evaluateTrack(time, track.first, track.second, false);
    Vec3 targetPosition=k.position,targetScale=k.scale;Quat targetRotation=k.rotation;
    Entity* parent=e->parentId?g.scene.byId(e->parentId):nullptr;
    if(k.localSpace&&parent&&parent->body){
        targetPosition=parent->body->position+parent->body->quat.rotate(mulComponents(k.position,parent->scale));
        targetRotation=(parent->body->quat*k.rotation).normalized();
        targetScale=mulComponents(k.scale,parent->scale);
    }
    Vec3 delta = targetPosition - e->body->position;
    Quat oldRot = e->body->quat;
    Vec3 oldScale = e->scale;
    e->body->position = targetPosition; e->body->quat = targetRotation; e->scale = targetScale;
    e->body->velocity = {}; e->body->angularVelocity = {};
    e->body->updateInertiaWorld(); g.scene.syncBodyShape(*e);
    g.scene.moveDescendants(e->id, delta);
    g.scene.rotateDescendants(e->id, e->body->position, oldRot, targetRotation);
    g.scene.scaleDescendants(e->id, e->body->position, targetRotation, oldScale, targetScale);
    }
}

static bool animationKeySelected(int index) {
    return std::find(g.animationSelectedKeys.begin(), g.animationSelectedKeys.end(), index) !=
           g.animationSelectedKeys.end();
}

static void animationClearKeySelection() {
    g.animationSelectedKeys.clear();
    g.animationSelectedKey = -1;
    g.animationSelectedEvent = -1;
    g.animationDraggingEvent = -1;
}

static bool animationEventNameUsed(const std::string& name,int skip=-1) {
    for(int i=0;i<(int)g.animationEdit.events.size();i++)
        if(i!=skip && g.animationEdit.events[i].name==name) return true;
    return false;
}

static std::string uniqueAnimationEventName(std::string base="AnimationEvent") {
    if(base.empty()) base="AnimationEvent";
    for(char& c:base) if(c==' '||c=='\t') c='_';
    if(!animationEventNameUsed(base)) return base;
    for(int suffix=2;;suffix++) {
        std::string candidate=base+"_"+std::to_string(suffix);
        if(!animationEventNameUsed(candidate)) return candidate;
    }
}

static void addAnimationEvent() {
    AnimationEventKey event;
    event.time=(std::max)(0.0f,g.animationTime);
    event.name=uniqueAnimationEventName();
    g.animationEdit.events.push_back(event);
    g.animationEdit.sortKeys();
    g.animationSelectedEvent=-1;
    for(int i=0;i<(int)g.animationEdit.events.size();i++)
        if(g.animationEdit.events[i].name==event.name){g.animationSelectedEvent=i;break;}
    g.animationSelectedKeys.clear();g.animationSelectedKey=-1;
    snprintf(g.animationEventName,sizeof(g.animationEventName),"%s",event.name.c_str());
}

static void animationDeleteSelectedEvent() {
    if(g.animationSelectedEvent<0||g.animationSelectedEvent>=(int)g.animationEdit.events.size())return;
    g.animationEdit.events.erase(g.animationEdit.events.begin()+g.animationSelectedEvent);
    g.animationEdit.sortKeys();
    g.animationTime=(std::min)(g.animationTime,g.animationEdit.length);
    g.animationSelectedEvent=-1;g.animationDraggingEvent=-1;
}

static void animationCopySelectedKeys() {
    g.animationKeyClipboard.clear();
    g.animationClipboardOrigin = (std::numeric_limits<float>::max)();
    for (int index : g.animationSelectedKeys) {
        if (index < 0 || index >= (int)g.animationEdit.keys.size()) continue;
        const AnimationKey& key = g.animationEdit.keys[index];
        g.animationKeyClipboard.push_back(key);
        g.animationClipboardOrigin = (std::min)(g.animationClipboardOrigin, key.time);
    }
    if (g.animationKeyClipboard.empty()) {
        addLog(2, "Select one or more keyframes to copy.");
        return;
    }
    addLog(0, "Copied %d keyframes.", (int)g.animationKeyClipboard.size());
}

static void animationPasteKeysAtCursor() {
    if (g.animationKeyClipboard.empty()) {
        addLog(2, "Keyframe clipboard empty: press Ctrl+C in the timeline first.");
        return;
    }
    restoreAnimationPreview();
    struct PastedIdentity { int entityId; std::string objectName; float time; };
    std::vector<PastedIdentity> pasted;
    for (const AnimationKey& source : g.animationKeyClipboard) {
        AnimationKey key = source;
        key.time = (std::max)(0.0f, g.animationTime + source.time - g.animationClipboardOrigin);
        g.animationEdit.keys.erase(std::remove_if(g.animationEdit.keys.begin(), g.animationEdit.keys.end(), [&](const AnimationKey& old) {
            bool sameTrack = key.entityId != 0 ? old.entityId == key.entityId
                                               : (old.entityId == 0 && old.objectName == key.objectName);
            return sameTrack && fabsf(old.time - key.time) < .002f;
        }), g.animationEdit.keys.end());
        g.animationEdit.keys.push_back(key);
        pasted.push_back({key.entityId, key.objectName, key.time});
    }
    g.animationEdit.sortKeys();
    animationClearKeySelection();
    for (int i = 0; i < (int)g.animationEdit.keys.size(); i++) {
        const AnimationKey& key = g.animationEdit.keys[i];
        for (const PastedIdentity& id : pasted) {
            bool sameTrack = id.entityId != 0 ? key.entityId == id.entityId
                                              : (key.entityId == 0 && key.objectName == id.objectName);
            if (sameTrack && fabsf(key.time - id.time) < .002f) {
                g.animationSelectedKeys.push_back(i);
                break;
            }
        }
    }
    if (!g.animationSelectedKeys.empty()) g.animationSelectedKey = g.animationSelectedKeys.back();
    applyAnimationPreview(g.animationTime);
    addLog(0, "Pasted %d keyframes at time %.2f.", (int)pasted.size(), g.animationTime);
}

static void animationDeleteSelectedKeys() {
    if (g.animationSelectedKeys.empty()) return;
    restoreAnimationPreview();
    std::sort(g.animationSelectedKeys.begin(), g.animationSelectedKeys.end(), std::greater<int>());
    for (int index : g.animationSelectedKeys)
        if (index >= 0 && index < (int)g.animationEdit.keys.size())
            g.animationEdit.keys.erase(g.animationEdit.keys.begin() + index);
    g.animationEdit.sortKeys();
    g.animationTime = (std::min)(g.animationTime, g.animationEdit.length);
    animationClearKeySelection();
    g.animationDraggingKey = -1;
    if (!g.animationEdit.keys.empty()) applyAnimationPreview(g.animationTime);
}

static void drawAnimationContent(UI& ui) {
    g.animationPanelDrawnThisFrame = true;
    Entity* e = g.scene.byId(g.selectedId);
    const UIInput& in=ui.input();
    bool animationShortcut = g.animationFocused && !ui.wantKeyboard();
    if (animationShortcut && in.keyCopy) animationCopySelectedKeys();
    if (animationShortcut && in.keyPaste) animationPasteKeysAtCursor();
    if (animationShortcut && in.keySelectAll) {
        animationClearKeySelection();
        for (int i=0;i<(int)g.animationEdit.keys.size();i++) g.animationSelectedKeys.push_back(i);
        if (!g.animationSelectedKeys.empty()) g.animationSelectedKey=g.animationSelectedKeys.back();
    }
    if (animationShortcut && in.keyDelete) {
        if(g.animationSelectedEvent>=0) animationDeleteSelectedEvent();
        else animationDeleteSelectedKeys();
    }
    ui.header("ANIMATION CLIP");
    ui.label(g.animationEditRel.empty() ? "Open or create a .anim asset from the Content Drawer." : g.animationEditRel,
             g.animationEditRel.empty() ? Vec3{.72f,.55f,.38f} : Vec3{.35f,.75f,1});
    ui.label(e ? std::string("Oggetto: ") + e->name : "Oggetto: nessuna selezione", { .68f,.74f,.84f });
    ui.row(6);
    if (ui.button("Save")) saveAnimationClip();
    if (ui.buttonColored(g.animationRecording ? "REC ON" : "REC", g.animationRecording ? Vec3{.62f,.08f,.10f}:Vec3{.25f,.12f,.14f}, {1,.78f,.78f})) {
        g.animationRecording = !g.animationRecording;
        if (e) { captureAnimationPreviewObjects(); AnimationKey k=selectedAnimationKey(); g.animationObservedEntity=e->id; g.animationObservedPos=k.position; g.animationObservedRot=k.rotation; g.animationObservedScale=k.scale; }
    }
    if (ui.button(g.animationPlaying ? "Pause" : "Play")) g.animationPlaying = !g.animationPlaying;
    if (ui.button("Stop")) { g.animationPlaying=false; g.animationRecording=false; g.animationTime=0; restoreAnimationPreview(); }
    if (ui.button("+ Keyframe")) addOrReplaceAnimationKey();
    if (ui.button("+ Event Trigger")) addAnimationEvent();
    ui.row(3);
    // Il cursore puo' essere portato molto oltre la durata corrente: la durata
    // della clip verra' estesa automaticamente quando si inserisce una chiave.
    ui.dragFloat("Tempo", &g.animationTime, .01f, 0, 1000000.0f);
    char durationText[64]; snprintf(durationText,sizeof(durationText),"Duration: %.2f s (last key)",g.animationEdit.length);
    ui.label(durationText,{.62f,.69f,.79f});
    ui.checkbox("Loop", &g.animationEdit.loop);

    if (g.animationPlaying && (!g.animationEdit.keys.empty()||!g.animationEdit.events.empty())) {
        g.animationTime += g.frameDt;
        if (g.animationTime > g.animationEdit.length) {
            if (g.animationEdit.loop) g.animationTime = fmodf(g.animationTime, std::max(.01f,g.animationEdit.length));
            else { g.animationTime = g.animationEdit.length; g.animationPlaying = false; }
        }
        applyAnimationPreview(g.animationTime);
    }
    if (g.animationRecording && e && e->body) {
        AnimationKey now=selectedAnimationKey();
        float dot=fabsf(now.rotation.x*g.animationObservedRot.x+now.rotation.y*g.animationObservedRot.y+now.rotation.z*g.animationObservedRot.z+now.rotation.w*g.animationObservedRot.w);
        if (g.animationObservedEntity != e->id) {
            g.animationObservedEntity=e->id; g.animationObservedPos=now.position;
            g.animationObservedRot=now.rotation; g.animationObservedScale=now.scale;
        } else if (now.position.distanceTo(g.animationObservedPos)>.0001f || now.scale.distanceTo(g.animationObservedScale)>.0001f || dot<.99999f) {
            addOrReplaceAnimationKey();
        }
    }

    struct AnimTrack { int entityId=0; std::string name; };
    std::vector<AnimTrack> tracks;
    auto addTrack=[&](int id,const std::string& name){
        for(AnimTrack&t:tracks) if((id!=0&&t.entityId==id)||(id==0&&t.entityId==0&&t.name==name)) { if(!name.empty())t.name=name; return; }
        tracks.push_back({id,name});
    };
    for(const AnimationKey& k:g.animationEdit.keys) addTrack(k.entityId,k.objectName);
    if(e){
        bool represented=false;
        for(const AnimTrack&t:tracks) if(t.entityId==e->id || (t.entityId==0&&t.name.empty())) represented=true;
        if(!represented)addTrack(e->id,e->name);
    }
    if(tracks.empty())tracks.push_back({0,"No object"});

    UIRect p=ui.panelInner(); float y=ui.panelCursorY()+7; const float labelW=170, headH=30, minRowH=34;
    float reservedBelow=(g.animationSelectedEvent>=0?142.0f:(!g.animationSelectedKeys.empty()?86.0f:48.0f));
    const int timelineRows=(int)tracks.size()+1; // la prima riga e' sempre dedicata agli eventi
    float minH=headH+minRowH*(float)timelineRows+8;
    float h=std::max(minH,p.y+p.h-y-reservedBelow);
    UIRect tl{p.x+8,y,p.w-16,h}; ui.spacing(h+14);
    Renderer* r=ui.r;
    UIRect panelClip=ui.panelClip();
    auto intersectRect=[](const UIRect& a,const UIRect& b){
        float x0=std::max(a.x,b.x),y0=std::max(a.y,b.y);
        float x1=std::min(a.x+a.w,b.x+b.w),y1=std::min(a.y+a.h,b.y+b.h);
        return UIRect{x0,y0,std::max(0.0f,x1-x0),std::max(0.0f,y1-y0)};
    };
    auto setPanelScissor=[&](const UIRect& rc){
        UIRect clipped=intersectRect(rc,panelClip);
        r->setUIScissor(clipped.x,clipped.y,clipped.w,clipped.h,true);
    };
    float graphX=tl.x+std::min(labelW,std::max(90.0f,tl.w*.32f)); float graphW=std::max(20.0f,tl.x+tl.w-graphX);
    UIRect tlVisible=intersectRect(tl,panelClip);
    bool inside=!ui.interactionBlocked()&&in.mouseX>=tlVisible.x&&in.mouseX<tlVisible.x+tlVisible.w&&in.mouseY>=tlVisible.y&&in.mouseY<tlVisible.y+tlVisible.h;
    bool graphInside=inside&&in.mouseX>=graphX;
    if(graphInside&&in.wheel!=0){
        float cursorTime=g.animationTimelineStart+(in.mouseX-graphX)/g.animationTimelinePixelsPerSecond;
        float next=clampf(g.animationTimelinePixelsPerSecond*powf(1.15f,in.wheel),5.0f,2000.0f);
        g.animationTimelineStart=(std::max)(0.0f,cursorTime-(in.mouseX-graphX)/next);
        g.animationTimelinePixelsPerSecond=next;
    }
    if(graphInside&&in.mmbPressed){g.animationTimelinePanning=true;g.animationTimelineLastMouseX=in.mouseX;}
    if(g.animationTimelinePanning){
        if(in.mmbDown){float dx=in.mouseX-g.animationTimelineLastMouseX;g.animationTimelineStart=(std::max)(0.0f,g.animationTimelineStart-dx/g.animationTimelinePixelsPerSecond);g.animationTimelineLastMouseX=in.mouseX;}
        else g.animationTimelinePanning=false;
    }
    if(g.animationPlaying){float viewEnd=g.animationTimelineStart+graphW/g.animationTimelinePixelsPerSecond;if(g.animationTime>viewEnd)g.animationTimelineStart=(std::max)(0.0f,g.animationTime-graphW/g.animationTimelinePixelsPerSecond*.85f);}
    auto timeX=[&](float time){return graphX+(time-g.animationTimelineStart)*g.animationTimelinePixelsPerSecond;};
    auto mouseTime=[&](float x){return g.animationTimelineStart+(x-graphX)/g.animationTimelinePixelsPerSecond;};
    float rowH=(h-headH-8)/(float)(std::max)(1,timelineRows);
    if(rowH<minRowH)rowH=minRowH;
    setPanelScissor(tl); r->drawRectPx(tl.x,tl.y,tl.w,tl.h,{.055f,.062f,.075f},1);
    r->drawRectPx(tl.x,tl.y,std::max(0.0f,graphX-tl.x),tl.h,{.075f,.083f,.098f},1);
    float rawStep=90.0f/g.animationTimelinePixelsPerSecond;
    float magnitude=powf(10.0f,floorf(log10f((std::max)(rawStep,.000001f))));
    float normalized=rawStep/magnitude;
    float tickStep=(normalized<=1?1:normalized<=2?2:normalized<=5?5:10)*magnitude;
    float viewEnd=g.animationTimelineStart+graphW/g.animationTimelinePixelsPerSecond;
    float firstTick=ceilf(g.animationTimelineStart/tickStep)*tickStep;
    for(float time=firstTick;time<=viewEnd+tickStep*.01f;time+=tickStep){float x=timeX(time);r->drawLinePx(x,tl.y,x,tl.y+tl.h,1,{.14f,.16f,.19f},1);char b[32];snprintf(b,sizeof(b),time<1000?"%.2f":"%.0f",time);r->drawTextLine(x+3,tl.y+4,b,{.48f,.54f,.64f},1);}
    int removeTrack=-1;
    const float eventRowY=tl.y+headH,eventCenterY=eventRowY+rowH*.5f;
    r->drawLinePx(tl.x,eventRowY,tl.x+tl.w,eventRowY,1,{.18f,.15f,.23f},1);
    for(int ei=0;ei<(int)g.animationEdit.events.size();ei++){
        float x=timeX(g.animationEdit.events[ei].time),s=7.0f;
        Vec3 color=ei==g.animationSelectedEvent?Vec3{1,.72f,.18f}:Vec3{.86f,.35f,.92f};
        r->drawTriPx(x,eventCenterY-s,x+s,eventCenterY,x,eventCenterY+s,color,1);
        r->drawTriPx(x,eventCenterY-s,x,eventCenterY+s,x-s,eventCenterY,color,1);
    }
    for(int ti=0;ti<(int)tracks.size();ti++){
        float rowY=tl.y+headH+(ti+1)*rowH; float centerY=rowY+rowH*.5f;
        r->drawLinePx(tl.x,rowY,tl.x+tl.w,rowY,1,{.12f,.135f,.16f},1);
        std::string label=tracks[ti].name;
        if(label.empty()){Entity*legacy=g.scene.byId(g.selectedId);label=legacy?legacy->name:"Legacy track";}
        std::vector<int> rowKeys;
        for(int ki=0;ki<(int)g.animationEdit.keys.size();ki++){
            const AnimationKey& k=g.animationEdit.keys[ki];
            if((tracks[ti].entityId!=0&&k.entityId==tracks[ti].entityId)||
               (tracks[ti].entityId==0&&k.entityId==0&&k.objectName==tracks[ti].name))rowKeys.push_back(ki);
        }
        std::sort(rowKeys.begin(),rowKeys.end(),[](int a,int b){return g.animationEdit.keys[a].time<g.animationEdit.keys[b].time;});
        float removeX=graphX-24;
        r->drawTextLine(tl.x+9,rowY+8,ui.ellipsize(label,graphX-tl.x-42),{.76f,.82f,.92f},1);
        if(!rowKeys.empty()){
            UIRect removeRc{removeX,centerY-12,19,24};
            bool removeHover=inside&&in.mouseX>=removeRc.x&&in.mouseX<removeRc.x+removeRc.w&&in.mouseY>=removeRc.y&&in.mouseY<removeRc.y+removeRc.h;
            if(removeHover)r->drawRectPx(removeRc.x,removeRc.y,removeRc.w,removeRc.h,{.38f,.12f,.14f},1);
            r->drawTextLine(removeRc.x+6,removeRc.y+1,"x",removeHover?Vec3{1,.62f,.62f}:Vec3{.62f,.66f,.72f},1);
            if(removeHover&&in.mouseReleased)removeTrack=ti;
        }
        for(int ri=1;ri<(int)rowKeys.size();ri++){
            float xa=timeX(g.animationEdit.keys[rowKeys[ri-1]].time);
            float xb=timeX(g.animationEdit.keys[rowKeys[ri]].time);
            r->drawLinePx(xa,centerY,xb,centerY,2,{.25f,.52f,.68f},.9f);
        }
        for(int ki:rowKeys){float x=timeX(g.animationEdit.keys[ki].time);Vec3 c=animationKeySelected(ki)?Vec3{1,.72f,.18f}:Vec3{.35f,.78f,1};r->drawRectPx(x-5,centerY-8,10,16,c,1);}
    }
    // Maschera netta fra intestazioni e tempo: keyframe fuori vista non devono
    // mai comparire sopra ai nomi delle tracce.
    r->drawRectPx(tl.x,tl.y,graphX-tl.x,tl.h,{.075f,.083f,.098f},1);
    r->drawLinePx(graphX-1,tl.y,graphX-1,tl.y+tl.h,1,{.18f,.21f,.25f},1);
    r->drawLinePx(tl.x,eventRowY,graphX,eventRowY,1,{.18f,.15f,.23f},1);
    r->drawTextLine(tl.x+9,eventRowY+8,"EVENT TRIGGER",{.91f,.55f,.96f},1);
    for(int ti=0;ti<(int)tracks.size();ti++){
        float rowY=tl.y+headH+(ti+1)*rowH,centerY=rowY+rowH*.5f;
        r->drawLinePx(tl.x,rowY,graphX,rowY,1,{.12f,.135f,.16f},1);
        std::string label=tracks[ti].name;
        if(label.empty()){Entity*legacy=g.scene.byId(g.selectedId);label=legacy?legacy->name:"Legacy track";}
        r->drawTextLine(tl.x+9,rowY+8,ui.ellipsize(label,graphX-tl.x-42),{.76f,.82f,.92f},1);
        bool hasKeys=false;for(const AnimationKey&k:g.animationEdit.keys){if((tracks[ti].entityId!=0&&k.entityId==tracks[ti].entityId)||(tracks[ti].entityId==0&&k.entityId==0&&k.objectName==tracks[ti].name)){hasKeys=true;break;}}
        if(hasKeys){float removeX=graphX-24;UIRect removeRc{removeX,centerY-12,19,24};bool hov=inside&&in.mouseX>=removeRc.x&&in.mouseX<removeRc.x+removeRc.w&&in.mouseY>=removeRc.y&&in.mouseY<removeRc.y+removeRc.h;if(hov)r->drawRectPx(removeRc.x,removeRc.y,removeRc.w,removeRc.h,{.38f,.12f,.14f},1);r->drawTextLine(removeRc.x+6,removeRc.y+1,"x",hov?Vec3{1,.62f,.62f}:Vec3{.62f,.66f,.72f},1);}
    }
    setPanelScissor({graphX,tl.y,graphW,tl.h});
    float playX=timeX(g.animationTime); r->drawLinePx(playX,tl.y,playX,tl.y+tl.h,2,{1,.25f,.25f},1);
    if(g.animationMarqueeSelecting&&in.mouseDown){float x0=(std::max)(graphX,(std::min)(g.animationMarqueeStartX,in.mouseX));float x1=(std::min)(graphX+graphW,(std::max)(g.animationMarqueeStartX,in.mouseX));float y0=(std::max)(tl.y,(std::min)(g.animationMarqueeStartY,in.mouseY));float y1=(std::min)(tl.y+tl.h,(std::max)(g.animationMarqueeStartY,in.mouseY));r->drawRectPx(x0,y0,x1-x0,y1-y0,{.30f,.62f,.99f},.12f);r->drawRectPx(x0,y0,x1-x0,1,{.30f,.62f,.99f},.9f);r->drawRectPx(x0,y1,x1-x0,1,{.30f,.62f,.99f},.9f);r->drawRectPx(x0,y0,1,y1-y0,{.30f,.62f,.99f},.9f);r->drawRectPx(x1,y0,1,y1-y0,{.30f,.62f,.99f},.9f);}
    r->setUIScissor(0,0,0,0,false); ui.reclipPanel(); ui.registerBlockingRect(tlVisible);
    if(removeTrack>=0&&removeTrack<(int)tracks.size()){
        AnimTrack doomed=tracks[removeTrack];
        restoreAnimationPreview();
        g.animationEdit.keys.erase(std::remove_if(g.animationEdit.keys.begin(),g.animationEdit.keys.end(),[&](const AnimationKey&k){
            return doomed.entityId!=0?k.entityId==doomed.entityId:(k.entityId==0&&k.objectName==doomed.name);
        }),g.animationEdit.keys.end());
        g.animationEdit.sortKeys();
        g.animationTime=std::min(g.animationTime,g.animationEdit.length);
        animationClearKeySelection();g.animationDraggingKey=-1;
        if(!g.animationEdit.keys.empty())applyAnimationPreview(g.animationTime);
        addLog(0,"Oggetto rimosso dalla Animation Clip: %s.",doomed.name.c_str());
    }
    auto keyTrackIndex=[&](const AnimationKey&k){for(int ti=0;ti<(int)tracks.size();ti++){bool same=tracks[ti].entityId!=0?k.entityId==tracks[ti].entityId:(k.entityId==0&&k.objectName==tracks[ti].name);if(same)return ti;}return -1;};
    if(graphInside&&in.mousePressed&&!g.animationTimelinePanning){
        int eventHit=-1;float eventBest=12;
        for(int i=0;i<(int)g.animationEdit.events.size();i++){float dx=fabsf(in.mouseX-timeX(g.animationEdit.events[i].time)),dy=fabsf(in.mouseY-eventCenterY);if(dx<eventBest&&dy<12){eventBest=dx;eventHit=i;}}
        int hit=-1;float best=11;
        if(eventHit<0)for(int i=0;i<(int)g.animationEdit.keys.size();i++){int ti=keyTrackIndex(g.animationEdit.keys[i]);if(ti<0)continue;float x=timeX(g.animationEdit.keys[i].time),cy=tl.y+headH+(ti+1)*rowH+rowH*.5f;float dx=fabsf(in.mouseX-x),dy=fabsf(in.mouseY-cy);if(dx<best&&dy<12){best=dx;hit=i;}}
        if(eventHit>=0){
            g.animationSelectedKeys.clear();g.animationSelectedKey=-1;
            g.animationSelectedEvent=eventHit;g.animationDraggingEvent=eventHit;
            g.animationEventDragOffset=mouseTime(in.mouseX)-g.animationEdit.events[eventHit].time;
            g.animationTime=g.animationEdit.events[eventHit].time;
            snprintf(g.animationEventName,sizeof(g.animationEventName),"%s",g.animationEdit.events[eventHit].name.c_str());
        }else if(hit>=0){
            g.animationSelectedEvent=-1;g.animationDraggingEvent=-1;
            bool remainsSelected=true;
            if(in.keyCtrl){auto it=std::find(g.animationSelectedKeys.begin(),g.animationSelectedKeys.end(),hit);if(it!=g.animationSelectedKeys.end()){g.animationSelectedKeys.erase(it);remainsSelected=false;}else g.animationSelectedKeys.push_back(hit);}
            else if(!animationKeySelected(hit)){animationClearKeySelection();g.animationSelectedKeys.push_back(hit);}
            if(remainsSelected){
                g.animationSelectedKey=hit;g.animationDraggingKey=hit;g.animationKeyDragMouseTime=mouseTime(in.mouseX);
                g.animationKeyDragStartTimes.assign(g.animationEdit.keys.size(),-1);
                for(int index:g.animationSelectedKeys)if(index>=0&&index<(int)g.animationEdit.keys.size())g.animationKeyDragStartTimes[index]=g.animationEdit.keys[index].time;
                g.animationTime=g.animationEdit.keys[hit].time;
            }else{g.animationSelectedKey=g.animationSelectedKeys.empty()?-1:g.animationSelectedKeys.back();g.animationDraggingKey=-1;}
        }else{
            g.animationSelectedEvent=-1;g.animationDraggingEvent=-1;
            g.animationMarqueeBaseSelection=in.keyCtrl?g.animationSelectedKeys:std::vector<int>{};
            if(!in.keyCtrl)animationClearKeySelection();
            g.animationMarqueeSelecting=true;g.animationMarqueeStartX=in.mouseX;g.animationMarqueeStartY=in.mouseY;
            g.animationTime=(std::max)(0.0f,mouseTime(in.mouseX));if(!g.animationPlaying)applyAnimationPreview(g.animationTime);
        }
    }
    if(g.animationMarqueeSelecting){
        if(in.mouseDown&&hypotf(in.mouseX-g.animationMarqueeStartX,in.mouseY-g.animationMarqueeStartY)>3){
            float x0=(std::min)(g.animationMarqueeStartX,in.mouseX),x1=(std::max)(g.animationMarqueeStartX,in.mouseX),y0=(std::min)(g.animationMarqueeStartY,in.mouseY),y1=(std::max)(g.animationMarqueeStartY,in.mouseY);
            g.animationSelectedKeys=g.animationMarqueeBaseSelection;
            for(int i=0;i<(int)g.animationEdit.keys.size();i++){int ti=keyTrackIndex(g.animationEdit.keys[i]);if(ti<0)continue;float x=timeX(g.animationEdit.keys[i].time),cy=tl.y+headH+(ti+1)*rowH+rowH*.5f;if(x+5>=x0&&x-5<=x1&&cy+8>=y0&&cy-8<=y1&&!animationKeySelected(i))g.animationSelectedKeys.push_back(i);}
            g.animationSelectedKey=g.animationSelectedKeys.empty()?-1:g.animationSelectedKeys.back();
        }
        if(!in.mouseDown)g.animationMarqueeSelecting=false;
    }
    if(g.animationDraggingEvent>=0&&g.animationDraggingEvent<(int)g.animationEdit.events.size()){
        if(in.mouseDown){
            AnimationEventKey& event=g.animationEdit.events[g.animationDraggingEvent];
            event.time=(std::max)(0.0f,mouseTime(in.mouseX)-g.animationEventDragOffset);
            g.animationTime=event.time;
            g.animationEdit.length=.01f;
            for(const AnimationKey& key:g.animationEdit.keys)g.animationEdit.length=(std::max)(g.animationEdit.length,key.time);
            for(const AnimationEventKey& marker:g.animationEdit.events)g.animationEdit.length=(std::max)(g.animationEdit.length,marker.time);
        }else{
            std::string selectedName=g.animationEdit.events[g.animationDraggingEvent].name;
            g.animationEdit.sortKeys();g.animationSelectedEvent=-1;
            for(int i=0;i<(int)g.animationEdit.events.size();i++)if(g.animationEdit.events[i].name==selectedName){g.animationSelectedEvent=i;break;}
            g.animationDraggingEvent=-1;
        }
    }
    if(g.animationDraggingKey>=0&&g.animationDraggingKey<(int)g.animationEdit.keys.size()){
        if(in.mouseDown){
            float delta=mouseTime(in.mouseX)-g.animationKeyDragMouseTime,minStart=(std::numeric_limits<float>::max)();
            for(int index:g.animationSelectedKeys)if(index>=0&&index<(int)g.animationKeyDragStartTimes.size()&&g.animationKeyDragStartTimes[index]>=0)minStart=(std::min)(minStart,g.animationKeyDragStartTimes[index]);
            if(minStart<(std::numeric_limits<float>::max)())delta=(std::max)(delta,-minStart);
            for(int index:g.animationSelectedKeys)if(index>=0&&index<(int)g.animationEdit.keys.size()&&index<(int)g.animationKeyDragStartTimes.size()&&g.animationKeyDragStartTimes[index]>=0)g.animationEdit.keys[index].time=g.animationKeyDragStartTimes[index]+delta;
            g.animationTime=g.animationEdit.keys[g.animationDraggingKey].time;g.animationEdit.length=.01f;for(const AnimationKey&key:g.animationEdit.keys)g.animationEdit.length=(std::max)(g.animationEdit.length,key.time);for(const AnimationEventKey&event:g.animationEdit.events)g.animationEdit.length=(std::max)(g.animationEdit.length,event.time);if(!g.animationPlaying)applyAnimationPreview(g.animationTime);
        }else{
            struct SelectedIdentity{int entityId;std::string objectName;float time;};std::vector<SelectedIdentity> selected;
            for(int index:g.animationSelectedKeys)if(index>=0&&index<(int)g.animationEdit.keys.size()){const AnimationKey&key=g.animationEdit.keys[index];selected.push_back({key.entityId,key.objectName,key.time});}
            g.animationEdit.sortKeys();animationClearKeySelection();
            for(int i=0;i<(int)g.animationEdit.keys.size();i++){const AnimationKey&key=g.animationEdit.keys[i];for(const SelectedIdentity&id:selected){bool same=id.entityId?key.entityId==id.entityId:(key.entityId==0&&key.objectName==id.objectName);if(same&&fabsf(key.time-id.time)<.0001f){g.animationSelectedKeys.push_back(i);break;}}}
            g.animationSelectedKey=g.animationSelectedKeys.empty()?-1:g.animationSelectedKeys.back();g.animationDraggingKey=-1;g.animationKeyDragStartTimes.clear();
        }
    }
    if(g.animationSelectedEvent>=0&&g.animationSelectedEvent<(int)g.animationEdit.events.size()){
        ui.header("SELECTED EVENT TRIGGER");
        if(ui.textInput("animation_event_name",g.animationEventName,sizeof(g.animationEventName))){
            for(char*c=g.animationEventName;*c;c++)if(*c==' '||*c=='\t')*c='_';
            std::string base=g.animationEventName;
            if(base.empty())base="AnimationEvent";
            std::string unique=base;
            for(int suffix=2;animationEventNameUsed(unique,g.animationSelectedEvent);suffix++)unique=base+"_"+std::to_string(suffix);
            g.animationEdit.events[g.animationSelectedEvent].name=unique;
            snprintf(g.animationEventName,sizeof(g.animationEventName),"%s",unique.c_str());
        }
        std::string selectedName=g.animationEdit.events[g.animationSelectedEvent].name;
        float eventTime=g.animationEdit.events[g.animationSelectedEvent].time;
        if(ui.dragFloat("Tempo evento",&eventTime,.01f,0,1000000.0f)){
            g.animationEdit.events[g.animationSelectedEvent].time=eventTime;
            g.animationTime=eventTime;g.animationEdit.sortKeys();g.animationSelectedEvent=-1;
            for(int i=0;i<(int)g.animationEdit.events.size();i++)if(g.animationEdit.events[i].name==selectedName){g.animationSelectedEvent=i;break;}
        }
        ui.label("The name is unique within the clip and appears on the Bind Animation Trigger node.",{.62f,.69f,.79f});
        ui.row(2);
        if(ui.button("Go to event")){g.animationTime=eventTime;float span=graphW/g.animationTimelinePixelsPerSecond;if(g.animationTime<g.animationTimelineStart||g.animationTime>g.animationTimelineStart+span)g.animationTimelineStart=(std::max)(0.0f,g.animationTime-span*.5f);applyAnimationPreview(g.animationTime);}
        if(ui.button("Delete event trigger"))animationDeleteSelectedEvent();
    }else if(g.animationSelectedKey>=0&&g.animationSelectedKey<(int)g.animationEdit.keys.size()&&!g.animationSelectedKeys.empty()){
        ui.row(2); if(ui.button("Go to keyframe")){g.animationTime=g.animationEdit.keys[g.animationSelectedKey].time;float span=graphW/g.animationTimelinePixelsPerSecond;if(g.animationTime<g.animationTimelineStart||g.animationTime>g.animationTimelineStart+span)g.animationTimelineStart=(std::max)(0.0f,g.animationTime-span*.5f);applyAnimationPreview(g.animationTime);}
        if(ui.button(g.animationSelectedKeys.size()>1?"Delete selected keyframes":"Delete keyframe"))animationDeleteSelectedKeys();
    }
    ui.label("Purple diamond: event trigger | Marquee/Ctrl: multi-selection | Ctrl+C/V: copy/paste | LMB drags | MMB pans | wheel zooms | S: key.", {.52f,.58f,.68f});
}

static bool saveAnimatorController() {
    if (g.animatorEditRel.empty()) return false;
    bool ok=writeFile(relAbs(g.animatorEditRel),g.animatorEdit.serialize());
    addLog(ok?1:2,ok?"Animator Controller saved: %s":"Saving the Animator failed: %s",g.animatorEditRel.c_str()); return ok;
}

static void previewAnimatorState(const AnimatorState& state) {
    std::string data;
    AnimationClipAsset opened;
    if(state.clip.empty()||!readFile(relAbs(state.clip),data)||!opened.deserialize(data)){addLog(2,"The state has no valid Animation Clip.");return;}
    restoreAnimationPreview();
    g.animationEdit=std::move(opened);
    g.animationEditRel=state.clip;g.animationTime=0;g.animationPlaying=true;animationClearKeySelection();
    g.animationTimelineStart=0;g.animationTimelinePixelsPerSecond=100;
    g.animationTimelinePanning=false;g.animationDraggingKey=-1;
    DockWindow* w=g.dock.find("animation"); if(w){if(!w->open)g.dock.toggle("animation");g.dock.setActive("animation");}
}

static void drawAnimatorContent(UI& ui) {
    g.animatorEdit.ensureDefaults();
    ui.header("ANIMATOR CONTROLLER");
    ui.label(g.animatorEditRel.empty()?"Open or create a .animctrl asset from the Content Drawer.":g.animatorEditRel,
             g.animatorEditRel.empty()?Vec3{.72f,.55f,.38f}:Vec3{.35f,.75f,1});
    ui.beginCenteredToolRow(1,26.0f);
    if(ui.toolIconButton("animator_save",0,false,"Save Animator Controller"))saveAnimatorController();
    ui.endCenteredToolRow();
    ui.row(3);
    if(ui.button("+ State")){int n=(int)g.animatorEdit.states.size()+1;g.animatorSelectedState=g.animatorEdit.addState("State "+std::to_string(n),"",50+n*18.0f,45+n*14.0f);g.animatorSelectedTransition=-1;}
    if(ui.button(g.animatorConnectFrom?"Cancel link":"Create transition")){g.animatorConnectFrom=g.animatorConnectFrom?0:g.animatorSelectedState;}
    if(ui.button("Delete state")&&g.animatorSelectedState){g.animatorEdit.removeState(g.animatorSelectedState);g.animatorSelectedState=0;g.animatorSelectedTransition=-1;g.animatorConnectFrom=0;}
    if(g.animatorConnectFrom)ui.label("Click the destination state. You can also create the reverse transition.",{1,.72f,.28f});

    UIRect full=ui.panelInner();const UIInput& layoutIn=ui.input();
    float maxVariable=(std::max)(160.0f,full.w-g.animatorInspectorWidth-300.0f-12.0f);
    g.animatorVariablesWidth=clampf(g.animatorVariablesWidth,160.0f,maxVariable);
    float variableW=g.animatorVariablesWidth;
    float variableSplitX=full.x+variableW+3.0f;
    UIRect variableSplitter{variableSplitX-7.0f,ui.panelCursorY(),14.0f,(std::max)(30.0f,full.y+full.h-ui.panelCursorY())};
    bool variableSplitHover=!ui.interactionBlocked()&&layoutIn.mouseX>=variableSplitter.x&&layoutIn.mouseX<variableSplitter.x+variableSplitter.w&&layoutIn.mouseY>=variableSplitter.y&&layoutIn.mouseY<variableSplitter.y+variableSplitter.h;
    if(variableSplitHover&&layoutIn.mousePressed&&!g.animatorInspectorResizing)g.animatorVariablesResizing=true;
    if(g.animatorVariablesResizing){
        if(layoutIn.mouseDown){g.animatorVariablesWidth=clampf(layoutIn.mouseX-full.x,160.0f,maxVariable);variableW=g.animatorVariablesWidth;}
        else g.animatorVariablesResizing=false;
    }
    float maxInspector=(std::max)(220.0f,full.w-variableW-300.0f-12.0f);
    g.animatorInspectorWidth=clampf(g.animatorInspectorWidth,220.0f,maxInspector);
    float splitX=full.x+full.w-g.animatorInspectorWidth-3.0f;
    UIRect inspectorSplitter{splitX-7.0f,ui.panelCursorY(),14.0f,(std::max)(30.0f,full.y+full.h-ui.panelCursorY())};
    bool splitHover=!ui.interactionBlocked()&&layoutIn.mouseX>=inspectorSplitter.x&&layoutIn.mouseX<inspectorSplitter.x+inspectorSplitter.w&&layoutIn.mouseY>=inspectorSplitter.y&&layoutIn.mouseY<inspectorSplitter.y+inspectorSplitter.h;
    if(splitHover&&layoutIn.mousePressed&&!g.animatorVariablesResizing)g.animatorInspectorResizing=true;
    if(g.animatorInspectorResizing){
        if(layoutIn.mouseDown)g.animatorInspectorWidth=clampf(full.x+full.w-layoutIn.mouseX,220.0f,maxInspector);
        else g.animatorInspectorResizing=false;
    }
    ui.beginColumns(variableW,g.animatorInspectorWidth);
    ui.header("VARIABLES");
    int addParameter=0;static const char* addParameterTypes[]={"Add...","Float","Bool","Trigger"};
    if(ui.combo("#animator_add_parameter",&addParameter,addParameterTypes,4)&&addParameter>0){
        AnimatorParameter parameter;parameter.type=addParameter-1;
        const char* prefix=parameter.type==ANIM_PARAM_FLOAT?"Float":parameter.type==ANIM_PARAM_BOOL?"Bool":"Trigger";
        parameter.name=std::string(prefix)+std::to_string(g.animatorEdit.parameters.size()+1);
        g.animatorEdit.parameters.push_back(parameter);g.animatorSelectedParameter=(int)g.animatorEdit.parameters.size()-1;
    }
    for(int i=0;i<(int)g.animatorEdit.parameters.size();i++){
        const AnimatorParameter&parameter=g.animatorEdit.parameters[i];std::string type=parameter.type==ANIM_PARAM_FLOAT?"Float":parameter.type==ANIM_PARAM_BOOL?"Bool":"Trigger";
        if(ui.selectable(("anim_param_"+std::to_string(i)).c_str(),parameter.name+"  ["+type+"]",i==g.animatorSelectedParameter))g.animatorSelectedParameter=i;
    }
    if(g.animatorSelectedParameter>=0&&g.animatorSelectedParameter<(int)g.animatorEdit.parameters.size()){
        AnimatorParameter&parameter=g.animatorEdit.parameters[g.animatorSelectedParameter];ui.header("VARIABLE DETAILS");
        snprintf(g.animatorParameterName,sizeof(g.animatorParameterName),"%s",parameter.name.c_str());
        if(ui.textInput("animator_parameter_name",g.animatorParameterName,sizeof(g.animatorParameterName))){for(char*c=g.animatorParameterName;*c;c++)if(*c==' ')*c='_';std::string old=parameter.name;parameter.name=g.animatorParameterName;for(AnimatorTransition&transition:g.animatorEdit.transitions)if(transition.parameter==old)transition.parameter=parameter.name;}
        static const char* parameterTypes[]={"Float","Bool","Trigger"};ui.combo("Type",&parameter.type,parameterTypes,3);
        if(parameter.type==ANIM_PARAM_FLOAT)ui.dragFloat("Default",&parameter.defaultValue,.02f,-1000000.0f,1000000.0f);
        else if(parameter.type==ANIM_PARAM_BOOL){bool value=parameter.defaultValue!=0;if(ui.checkbox("Default",&value))parameter.defaultValue=value?1.0f:0.0f;}
        if(ui.button("Remove")){std::string old=parameter.name;g.animatorEdit.parameters.erase(g.animatorEdit.parameters.begin()+g.animatorSelectedParameter);for(AnimatorTransition&transition:g.animatorEdit.transitions)if(transition.parameter==old){transition.parameter.clear();transition.condition=0;}g.animatorSelectedParameter=-1;}
    }
    ui.nextColumn();
    UIRect p=ui.panelInner();float y=ui.panelCursorY()+5;float h=std::max(250.0f,p.y+p.h-y-10);UIRect gr{p.x+6,y,p.w-12,h};ui.spacing(h+10);
    Renderer* r=ui.r;const UIInput& in=ui.input();
    bool graphHover=!ui.interactionBlocked()&&in.mouseX>=gr.x&&in.mouseX<gr.x+gr.w&&in.mouseY>=gr.y&&in.mouseY<gr.y+gr.h;
    if(graphHover&&in.wheel!=0){float old=g.animatorZoom;g.animatorZoom=clampf(old*powf(1.12f,in.wheel),.35f,2.5f);float wx=(in.mouseX-gr.x-g.animatorPanX)/old,wy=(in.mouseY-gr.y-g.animatorPanY)/old;g.animatorPanX=in.mouseX-gr.x-wx*g.animatorZoom;g.animatorPanY=in.mouseY-gr.y-wy*g.animatorZoom;}
    if(graphHover&&in.rmbPressed){g.animatorPanning=true;g.animatorLastMouseX=in.mouseX;g.animatorLastMouseY=in.mouseY;}
    if(g.animatorPanning&&in.rmbDown){g.animatorPanX+=in.mouseX-g.animatorLastMouseX;g.animatorPanY+=in.mouseY-g.animatorLastMouseY;g.animatorLastMouseX=in.mouseX;g.animatorLastMouseY=in.mouseY;}else if(!in.rmbDown)g.animatorPanning=false;
    float Z=g.animatorZoom;
    r->setUIScissor(gr.x,gr.y,gr.w,gr.h,true);r->drawRectPx(gr.x,gr.y,gr.w,gr.h,{.05f,.057f,.068f},1);
    float gridStep=24*Z;
    for(float x=gr.x+fmodf(g.animatorPanX,gridStep);x<gr.x+gr.w;x+=gridStep)r->drawLinePx(x,gr.y,x,gr.y+gr.h,1,{.09f,.105f,.125f},1);
    for(float yy=gr.y+fmodf(g.animatorPanY,gridStep);yy<gr.y+gr.h;yy+=gridStep)r->drawLinePx(gr.x,yy,gr.x+gr.w,yy,1,{.09f,.105f,.125f},1);
    auto stateCenter=[&](const AnimatorState&s){return Vec3{gr.x+g.animatorPanX+(s.x+72)*Z,gr.y+g.animatorPanY+(s.y+29)*Z,0};};
    int activeState=0,previousState=0;float activeProgress=0;
    for(const Entity&entity:g.scene.entities)if(entity.hasAnimator&&g.animatorEditRel==entity.animatorController&&entity.animatorRuntimeState){activeState=entity.animatorRuntimeState;previousState=entity.animatorRuntimePreviousState;const AnimatorState*as=g.animatorEdit.byId(activeState);AnimationClipAsset*clip=as?runtimeAnimationClip(as->clip):nullptr;if(clip)activeProgress=clampf(entity.animatorRuntimeTime/(std::max)(.01f,clip->length),0,1);break;}
    // Entry e' editor-only, sempre presente e non selezionabile/distruttibile.
    UIRect entry{gr.x+g.animatorPanX+20*Z,gr.y+g.animatorPanY+88*Z,90*Z,42*Z};
    const AnimatorState* initial=g.animatorEdit.byId(g.animatorEdit.defaultState);
    if(initial){Vec3 a{entry.x+entry.w,entry.y+entry.h*.5f,0},b=stateCenter(*initial);r->drawLinePx(a.x,a.y,b.x,b.y,2,{.35f,1,.55f},1);float dx=b.x-a.x,dy=b.y-a.y,len=sqrtf(dx*dx+dy*dy);if(len>1){dx/=len;dy/=len;float ax=b.x-dx*20*Z,ay=b.y-dy*20*Z;r->drawLinePx(ax,ay,ax-dx*9*Z-dy*5*Z,ay-dy*9*Z+dx*5*Z,2,{.35f,1,.55f},1);r->drawLinePx(ax,ay,ax-dx*9*Z+dy*5*Z,ay-dy*9*Z-dx*5*Z,2,{.35f,1,.55f},1);}}
    r->drawRectPx(entry.x,entry.y,entry.w,entry.h,{.16f,.38f,.22f},1);r->drawTextLine(entry.x+12*Z,entry.y+11*Z,"Enter",{.75f,1,.82f},1,Z);
    for(int ti=0;ti<(int)g.animatorEdit.transitions.size();ti++){
        const AnimatorTransition&t=g.animatorEdit.transitions[ti];const AnimatorState*a=g.animatorEdit.byId(t.from),*b=g.animatorEdit.byId(t.to);if(!a||!b)continue;Vec3 pa=stateCenter(*a),pb=stateCenter(*b);float centerDx=pb.x-pa.x,centerDy=pb.y-pa.y;if(fabsf(centerDx)>=fabsf(centerDy)){float side=centerDx>=0?1.0f:-1.0f;pa.x+=side*72*Z;pb.x-=side*72*Z;}else{float side=centerDy>=0?1.0f:-1.0f;pa.y+=side*29*Z;pb.y-=side*29*Z;}float dx=pb.x-pa.x,dy=pb.y-pa.y,len=sqrtf(dx*dx+dy*dy);if(len<1)continue;dx/=len;dy/=len;bool reciprocal=g.animatorEdit.transition(t.to,t.from)!=nullptr;float offset=reciprocal?8.0f*Z:0;float ox=-dy*offset,oy=dx*offset;float x1=pa.x+ox,y1=pa.y+oy,x2=pb.x+ox,y2=pb.y+oy;bool activeArrow=previousState==t.from&&activeState==t.to;Vec3 color=ti==g.animatorSelectedTransition?Vec3{1,.72f,.18f}:activeArrow?Vec3{1,1,1}:Vec3{.62f,.70f,.82f};r->drawLinePx(x1,y1,x2,y2,ti==g.animatorSelectedTransition||activeArrow?3.0f:2.0f,color,1);float hx=x1+(x2-x1)*.72f,hy=y1+(y2-y1)*.72f;r->drawLinePx(hx,hy,hx-dx*10*Z-dy*5*Z,hy-dy*10*Z+dx*5*Z,2,color,1);r->drawLinePx(hx,hy,hx-dx*10*Z+dy*5*Z,hy-dy*10*Z-dx*5*Z,2,color,1);
        if(graphHover&&in.mousePressed){float vx=x2-x1,vy=y2-y1,l2=vx*vx+vy*vy,u=l2>0?clampf(((in.mouseX-x1)*vx+(in.mouseY-y1)*vy)/l2,0,1):0,qx=x1+vx*u-in.mouseX,qy=y1+vy*u-in.mouseY;if(qx*qx+qy*qy<64){g.animatorSelectedTransition=ti;g.animatorSelectedState=0;}}
    }
    int clicked=0;
    for(auto&s:g.animatorEdit.states){UIRect nr{gr.x+g.animatorPanX+s.x*Z,gr.y+g.animatorPanY+s.y*Z,144*Z,58*Z};bool hov=graphHover&&!g.animatorPanning&&in.mouseX>=nr.x&&in.mouseX<nr.x+nr.w&&in.mouseY>=nr.y&&in.mouseY<nr.y+nr.h;Vec3 c=s.id==g.animatorEdit.defaultState?Vec3{.20f,.46f,.30f}:Vec3{.16f,.18f,.22f};r->drawRectPx(nr.x,nr.y,nr.w,nr.h,c,1);Vec3 border=(s.id==g.animatorSelectedState||s.id==activeState)?Vec3{1,1,1}:Vec3{.35f,.7f,1};float bw=(s.id==g.animatorSelectedState||s.id==activeState)?2*Z:1*Z;r->drawRectPx(nr.x,nr.y,nr.w,bw,border,1);r->drawRectPx(nr.x,nr.y+nr.h-bw,nr.w,bw,border,1);r->drawRectPx(nr.x,nr.y,bw,nr.h,border,1);r->drawRectPx(nr.x+nr.w-bw,nr.y,bw,nr.h,border,1);std::string stateName=ui.ellipsize(s.name,(nr.w-18*Z)/Z);r->drawTextLine(nr.x+9*Z,nr.y+10*Z,stateName,{.9f,.94f,1},1,Z);std::string clip=s.clip.empty()?"No clip":fs::path(s.clip).stem().string();clip=ui.ellipsize(clip,(nr.w-18*Z)/Z);r->drawTextLine(nr.x+9*Z,nr.y+32*Z,clip,{.58f,.66f,.76f},1,Z);if(s.id==activeState)r->drawRectPx(nr.x,nr.y+nr.h-4*Z,nr.w*activeProgress,4*Z,{1,1,1},1);if(hov&&in.mousePressed){clicked=s.id;if(g.animatorConnectFrom&&g.animatorConnectFrom!=s.id){if(g.animatorEdit.connect(g.animatorConnectFrom,s.id))g.animatorSelectedTransition=(int)g.animatorEdit.transitions.size()-1;g.animatorConnectFrom=0;g.animatorSelectedState=0;}else{g.animatorSelectedState=s.id;g.animatorSelectedTransition=-1;g.animatorDragState=s.id;g.animatorDragOffX=in.mouseX-nr.x;g.animatorDragOffY=in.mouseY-nr.y;}}}
    if(g.animatorDragState){AnimatorState*s=g.animatorEdit.byId(g.animatorDragState);if(s&&in.mouseDown){s->x=(in.mouseX-gr.x-g.animatorPanX-g.animatorDragOffX)/Z;s->y=(in.mouseY-gr.y-g.animatorPanY-g.animatorDragOffY)/Z;}else g.animatorDragState=0;}
    r->setUIScissor(0,0,0,0,false);ui.reclipPanel();ui.registerBlockingRect(gr);
    ui.nextColumn();
    AnimatorState* selected=g.animatorEdit.byId(g.animatorSelectedState);
    if(selected){ui.header("SELECTED STATE");if(g.animatorNameState!=selected->id){g.animatorNameState=selected->id;snprintf(g.animatorStateName,sizeof(g.animatorStateName),"%s",selected->name.c_str());}if(ui.textInput("animator_state_name",g.animatorStateName,sizeof(g.animatorStateName)))selected->name=g.animatorStateName;std::vector<std::string> paths,labels;collectAnimationClips(paths,labels);std::vector<const char*> names;for(auto&x:labels)names.push_back(x.c_str());int ci=0;for(int i=0;i<(int)paths.size();i++)if(paths[i]==selected->clip)ci=i;if(!names.empty()&&ui.combo("Animation Clip",&ci,names.data(),(int)names.size()))selected->clip=paths[ci];ui.dragFloat("Speed",&selected->speed,.01f,0,20);ui.checkbox("Mirror",&selected->mirror);if(ui.button("Set as initial state"))g.animatorEdit.defaultState=selected->id;if(ui.button("Preview"))previewAnimatorState(*selected);if(ui.button("Remove the state's transitions")){g.animatorEdit.transitions.erase(std::remove_if(g.animatorEdit.transitions.begin(),g.animatorEdit.transitions.end(),[&](const AnimatorTransition&t){return t.from==selected->id||t.to==selected->id;}),g.animatorEdit.transitions.end());g.animatorSelectedTransition=-1;}}
    else if(g.animatorSelectedTransition>=0&&g.animatorSelectedTransition<(int)g.animatorEdit.transitions.size()){
        AnimatorTransition&t=g.animatorEdit.transitions[g.animatorSelectedTransition];const AnimatorState*from=g.animatorEdit.byId(t.from),*to=g.animatorEdit.byId(t.to);ui.header("TRANSITION");ui.label((from?from->name:"?")+std::string("  ->  ")+(to?to->name:"?"),{.9f,.82f,.55f});ui.dragFloat("Durata blend (s)",&t.duration,.01f,0,10);static const char*curves[]={"Linear","Ease In/Out","Ease In","Ease Out"};ui.combo("Curva blend",&t.blendCurve,curves,4);ui.checkbox("Use Exit Time",&t.hasExitTime);if(t.hasExitTime)ui.dragFloat("Normalized Exit Time",&t.exitTime,.01f,0,10);
        std::vector<const char*>parameterNames;parameterNames.push_back("(none)");for(const AnimatorParameter&p:g.animatorEdit.parameters)parameterNames.push_back(p.name.c_str());int pi=0;for(int i=0;i<(int)g.animatorEdit.parameters.size();i++)if(g.animatorEdit.parameters[i].name==t.parameter)pi=i+1;if(ui.combo("Variable",&pi,parameterNames.data(),(int)parameterNames.size())){t.parameter=pi?g.animatorEdit.parameters[pi-1].name:"";t.condition=0;}if(pi){const AnimatorParameter&p=g.animatorEdit.parameters[pi-1];if(p.type==ANIM_PARAM_FLOAT){static const char*ops[]={"Sempre","> threshold","< threshold"};int op=t.condition==3?1:t.condition==4?2:0;if(ui.combo("Condition",&op,ops,3))t.condition=op==1?3:op==2?4:0;if(t.condition==3||t.condition==4)ui.dragFloat("Threshold",&t.threshold,.02f,-1000000,1000000);}else if(p.type==ANIM_PARAM_BOOL){static const char*ops[]={"Sempre","True","False"};int op=t.condition==1?1:t.condition==2?2:0;if(ui.combo("Condition",&op,ops,3))t.condition=op==1?1:op==2?2:0;}else{static const char*ops[]={"Sempre","Trigger"};int op=t.condition==5?1:0;if(ui.combo("Condition",&op,ops,2))t.condition=op?5:0;}}
        if(!g.animatorEdit.transition(t.to,t.from)&&ui.button("Create reverse transition"))g.animatorEdit.connect(t.to,t.from);if(ui.button("Delete transition")){g.animatorEdit.transitions.erase(g.animatorEdit.transitions.begin()+g.animatorSelectedTransition);g.animatorSelectedTransition=-1;}
    }else{ui.header("DETTAGLI");ui.label("Select a state or an arrow.",{.55f,.59f,.66f});ui.label("Enter always points at the initial state and cannot be deleted.",{.45f,.72f,.52f});}
    ui.label("RMB pans | wheel zooms | drag the states.",{.52f,.58f,.68f});ui.endColumns();
    variableSplitX=full.x+g.animatorVariablesWidth+3.0f;
    variableSplitter.x=variableSplitX-7.0f;
    Vec3 variableSplitterColor=(variableSplitHover||g.animatorVariablesResizing)?Vec3{.28f,.62f,.96f}:Vec3{.22f,.26f,.32f};
    ui.r->drawRectPx(variableSplitX-2.0f,variableSplitter.y,4.0f,variableSplitter.h,variableSplitterColor,1);
    ui.registerBlockingRect(variableSplitter);
    splitX=full.x+full.w-g.animatorInspectorWidth-3.0f;
    inspectorSplitter.x=splitX-7.0f;
    Vec3 splitterColor=(splitHover||g.animatorInspectorResizing)?Vec3{.28f,.62f,.96f}:Vec3{.22f,.26f,.32f};
    ui.r->drawRectPx(splitX-2.0f,inspectorSplitter.y,4.0f,inspectorSplitter.h,splitterColor,1);
    ui.registerBlockingRect(inspectorSplitter);
}

static void windowContent(UI& ui, const std::string& id) {
    if (id == "outliner") drawOutlinerContent(ui);
    else if (id == "dettagli") drawDetailsContent(ui);
    else if (id == "log") drawLogContent(ui);
    else if (id == "contenuti") drawContenutiContent(ui);
    else if (id == "navigation") drawNavigationContent(ui);
    else if (id == "animation") drawAnimationContent(ui);
    else if (id == "animator") drawAnimatorContent(ui);
}

static UIRect buildWindowRect() {
    float w = clampf((float)g.width - 120.0f, 560.0f, 860.0f);
    float h = clampf((float)g.height - TOP_H - 80.0f, 430.0f, 680.0f);
    return { ((float)g.width - w) * 0.5f, TOP_H + 28.0f, w, h };
}

static void drawBuildWindow() {
    if (!g.buildWindowOpen) return;
    if (!g.buildScenesScanned) scanBuildScenes(false);
    UI& ui = g.ui;
    UIRect rc = buildWindowRect();
    ui.panelBegin("build_windows", rc.x, rc.y, rc.w, rc.h, "BUILD WINDOWS");
    ui.label("Build a playtestable Windows version of the project.", { 0.80f, 0.86f, 0.94f });
    ui.label("The first included scene is the startup scene.", { 0.55f, 0.65f, 0.76f });
    ui.spacing(5);
    ui.header("DESTINATION");
    if (ui.button("Choose folder...")) {
        char path[MAX_PATH] = "";
        if (pickFolder(path, MAX_PATH, "Choose the build output folder"))
            snprintf(g.buildOutput, MAX_PATH, "%s", path);
    }
    ui.label(g.buildOutput[0] ? ui.ellipsize(g.buildOutput, rc.w - 42) : "No folder selected",
             g.buildOutput[0] ? Vec3{ 0.72f, 0.80f, 0.90f } : Vec3{ 0.90f, 0.55f, 0.42f });
    ui.spacing(5);
    ui.header("SCENES IN BUILD (EXECUTION ORDER)");
    if (g.buildScenes.empty()) {
        ui.label("No .imp scene found in the project.", { 0.90f, 0.55f, 0.42f });
    } else {
        for (int i = 0; i < (int)g.buildScenes.size(); i++) {
            const auto& scene = g.buildScenes[i];
            char id[48]; snprintf(id, sizeof(id), "build_scene_%d", i);
            std::string label = std::string(scene.include ? "[x]  " : "[ ]  ") +
                                std::to_string(i + 1) + ".  " + scene.rel;
            if (ui.selectable(id, ui.ellipsize(label, rc.w - 46), g.buildSceneSelected == i))
                g.buildSceneSelected = i;
        }
        ui.row(3);
        if (ui.button(g.buildSceneSelected >= 0 && !g.buildScenes[g.buildSceneSelected].include
                      ? "Include scene" : "Exclude scene")) {
            if (g.buildSceneSelected >= 0 && g.buildSceneSelected < (int)g.buildScenes.size())
                g.buildScenes[g.buildSceneSelected].include = !g.buildScenes[g.buildSceneSelected].include;
        }
        if (ui.button("Move up")) {
            int i = g.buildSceneSelected;
            if (i > 0) { std::swap(g.buildScenes[i], g.buildScenes[i - 1]); g.buildSceneSelected--; }
        }
        if (ui.button("Move down")) {
            int i = g.buildSceneSelected;
            if (i >= 0 && i + 1 < (int)g.buildScenes.size()) {
                std::swap(g.buildScenes[i], g.buildScenes[i + 1]); g.buildSceneSelected++;
            }
        }
    }
    ui.spacing(5);
    ui.row(3);
    if (ui.button("Refresh scenes")) scanBuildScenes(true);
    if (ui.buttonColored("BUILD WINDOWS", { 0.10f, 0.34f, 0.20f }, { 0.68f, 1.0f, 0.76f })) packageWindowsBuild();
    if (ui.button("Close")) g.buildWindowOpen = false;
    if (!g.buildStatus.empty()) {
        ui.spacing(5);
        ui.labelWrapped(g.buildStatus, { 0.68f, 0.80f, 0.94f });
    }
    ui.panelEnd();
}

static void drawMinimalPlayBar() {
    UI& ui = g.ui;
    ui.menuBarBegin(MENUBAR_H);
    ui.barLabel(g.projectName.empty() ? "PULSE GAME" : g.projectName,
                { 0.30f, 0.62f, 0.99f });
    ui.barSpace(16);
    if (ui.barButton(g.paused ? "> Resume" : "|| Pause",
                     { 0.30f, 0.28f, 0.12f }, { 1.0f, 0.95f, 0.70f })) g.paused = !g.paused;
    if (ui.barButton(g.standaloneMode ? "# Quit" : "# Stop",
                     { 0.40f, 0.13f, 0.13f }, { 1.0f, 0.75f, 0.75f })) {
        if (g.standaloneMode) g.running = false;
        else stopPlay();
    }
    bool full = g.windowFullscreen;
    if (ui.barCheckbox("Fullscreen", &full)) setWindowFullscreen(full);
    ui.menuBarEnd();
}

// Save just the asset shown in the active document tab (the scene on the Level
// tab). Wired to the tab-bar Save button and Ctrl+S.
static void saveActiveDoc() {
    if (g.mode == Mode::Play) { addLog(2, "Stop the simulation before saving."); return; }
    if (g.prefabEditMode) { savePrefabEdit(); return; }
    if (g.activeDoc == 0) {                       // Level tab → save the scene
        // an animation clip / animator being authored on the level lives in its own file
        if (g.animationFocused || !g.animationEditRel.empty()) saveAnimationClip();
        if (!g.animatorEditRel.empty()) saveAnimatorController();
        saveProject(false);
        return;
    }
    if (BPEditor* bp = activeBP()) {
        if (bp->curPath.empty()) { addLog(2, "Blueprint has no path: create it from the Content Browser."); return; }
        if (bp->saveTo(g.projectDir + "\\" + bp->curPath)) addLog(1, "Blueprint saved: %s", bp->curPath.c_str());
        else addLog(2, "Could not save the Blueprint: %s", bp->curPath.c_str());
        return;
    }
    if (CurveEditor* cv = activeCurve()) { cv->save(); return; }
    if (MaterialEditor* mt = activeMaterial()) { mt->save(); return; }
    if (WidgetEditor* wd = activeWidget()) { wd->save(); return; }
}

// Save the whole project: the scene plus every open asset with unsaved changes.
static void saveAllProject() {
    if (g.mode == Mode::Play) { addLog(2, "Stop the simulation before saving."); return; }
    if (g.prefabEditMode) { savePrefabEdit(); return; }
    int saved = 0;
    for (auto& d : g.bpDocs)
        if (d && d->dirty && !d->curPath.empty() && d->saveTo(g.projectDir + "\\" + d->curPath)) saved++;
    for (auto& d : g.curveDocs)    if (d && d->dirty && d->save()) saved++;
    for (auto& d : g.materialDocs) if (d && d->dirty && d->save()) saved++;
    for (auto& d : g.widgetDocs)   if (d && d->isDirty() && d->save()) saved++;
    if (g.animationFocused || !g.animationEditRel.empty()) saveAnimationClip();
    if (!g.animatorEditRel.empty()) saveAnimatorController();
    saveProject(false);   // the scene (.imp)
    addLog(1, "Project saved: scene + %d assets.", saved);
}

static void drawMenuBar() {
    UI& ui = g.ui;
    ui.menuBarBegin(MENUBAR_H);
    ui.barLabel("PULSE", { 0.30f, 0.62f, 0.99f });

    if (ui.menuBegin("File")) {
        if (ui.menuItem("Nuova scena")) {
            stopPlay();
            g.scene.clear();
            g.navigation.baked = false;
            g.navigation.cells.clear();
            spawnPrefab(5);
            g.selectedId = 0;
            g.projectPath[0] = 0;
            clearSceneHistory();
            addLog(1, "Nuova scena creata.");
        }
        if (ui.menuItem("Open scene... (.imp)")) openProject();
        ui.menuSeparator();
        if (ui.menuItem("Save scene             Ctrl+S")) saveActiveDoc();
        if (ui.menuItem("Save project (all)     Ctrl+Shift+S")) saveAllProject();
        if (ui.menuItem("Save scene as...")) saveProject(true);
        if (ui.menuItem("Save selection as prefab...")) savePrefab();
        ui.menuSeparator();
        if (ui.menuItem("Project settings...")) g.settingsWindowOpen = true;
        ui.menuSeparator();
        if (ui.menuItem("Close project (back to the Hub)")) returnToHub();
        if (ui.menuItem("Quit")) g.running = false;
        ui.menuEnd();
    }
    if (ui.menuBegin("Edit")) {
        if (ui.menuItem("Copy             Ctrl+C")) copySelection();
        if (ui.menuItem("Paste            Ctrl+V")) pasteClipboard();
        if (ui.menuItem("Duplicate selection")) {
            Entity* sel = g.scene.byId(g.selectedId);
            duplicateSceneEntity(sel ? sel->id : 0);
        }
        if (ui.menuItem("Delete selection    Del")) {
            Entity* sel = g.scene.byId(g.selectedId);
            if (sel) {
                addLog(0, "Deleted: %s", sel->name);
                std::vector<int> audioIds; g.scene.collectSubtree(sel->id, audioIds);
                for (int audioId : audioIds) g.audio.stop(audioId);
                g.scene.removeEntity(sel->id);
                g.selectedId = 0;
            }
        }
        ui.menuEnd();
    }
    if (ui.menuBegin("Add")) {
        ui.menuLabel("OGGETTI BASE");
        if (ui.menuItem("Empty Object")) spawnPrefab(12);
        ui.menuSeparator();
        ui.menuLabel("3D GEOMETRY");
        if (ui.menuItem("Cube")) spawnPrefab(0);
        if (ui.menuItem("Sphere")) spawnPrefab(1);
        if (ui.menuItem("Cylinder")) spawnPrefab(2);
        if (ui.menuItem("Cone")) spawnPrefab(11);
        if (ui.menuItem("Capsule")) spawnPrefab(8);
        ui.menuSeparator();
        ui.menuLabel("GAMEPLAY AND COLLISIONS");
        if (ui.menuItem("Trigger volume (invisible)")) spawnPrefab(9);
        if (ui.menuItem("Pendulum with constraint")) spawnPrefab(7);
        if (ui.menuItem("Physics Constraint")) spawnPrefab(16);
        ui.menuSeparator();
        ui.menuLabel("RENDERING AND AUDIO");
        if (ui.menuItem("Camera")) spawnPrefab(10);
        if (ui.menuItem("Luce puntuale")) spawnPrefab(3);
        if (ui.menuItem("Audio Source")) spawnPrefab(13);
        if (ui.menuItem("Audio Reverb Zone")) spawnPrefab(14);
        ui.menuSeparator();
        ui.menuLabel("ARTIFICIAL INTELLIGENCE");
        if (ui.menuItem("AI Agent")) spawnPrefab(15);
        ui.menuSeparator();
        ui.menuLabel("ENVIRONMENT");
        if (ui.menuItem("Static wall")) spawnPrefab(4);
        if (ui.menuItem("Floor")) spawnPrefab(5);
        ui.menuEnd();
    }
    if (ui.menuBegin("Simulation")) {
        if (ui.menuItem(g.mode == Mode::Play ? "Running..." : "Start (Play)")) play();
        if (ui.menuItem(g.paused ? "Resume" : "Pause")) {
            if (g.mode == Mode::Play) g.paused = !g.paused;
        }
        if (ui.menuItem("Single step")) {
            if (g.mode == Mode::Play) {
                g.paused = true;
                applyGrabForce();
                g.scene.world.step(FIXED_DT);
                propagateAttachments();
            }
        }
        if (ui.menuItem("Stop and restore")) stopPlay();
        ui.menuEnd();
    }
    if (ui.menuBegin("Build")) {
        if (ui.menuItem("Build Settings...")) {
            g.buildWindowOpen = true;
            scanBuildScenes(true);
        }
        if (ui.menuItem("Build Windows")) {
            if (!g.buildOutput[0]) {
                g.buildWindowOpen = true;
                g.buildStatus = "Choose the destination folder.";
                scanBuildScenes(true);
            } else packageWindowsBuild();
        }
        ui.menuEnd();
    }
    if (ui.menuBegin("Windows")) {
        const char* ids[8] = { "viewport", "outliner", "dettagli", "log", "contenuti", "navigation", "animation", "animator" };
        const char* titles[8] = { "Viewport", "Outliner", "Details", "Log", "Content", "Navigation", "Animation", "Animator Controller" };
        for (int i = 0; i < 8; i++) {
            DockWindow* w = g.dock.find(ids[i]);
            char label[64];
            snprintf(label, sizeof(label), "%s %s", w && w->open ? "[x]" : "[  ]", titles[i]);
            if (ui.menuItem(label)) g.dock.toggle(ids[i]);
        }
        ui.menuSeparator();
        if (ui.menuItem(g.editorFullscreen ? "[x] Fullscreen application   F11"
                                           : "[ ] Fullscreen application   F11"))
            toggleEditorFullscreen();
        ui.menuEnd();
    }

    ui.barSpace(24);
    if (g.mode == Mode::Edit) {
        if (ui.barButton("> Play", { 0.12f, 0.35f, 0.18f }, { 0.6f, 1, 0.7f })) play();
        ui.barCheckbox("Play fullscreen", &g.playFullscreenOption);
    } else {
        if (ui.barButton(g.paused ? ">> Resume" : "|| Pause", { 0.3f, 0.28f, 0.12f }, { 1, 0.95f, 0.7f })) g.paused = !g.paused;
        if (ui.barButton("# Stop", { 0.4f, 0.13f, 0.13f }, { 1, 0.75f, 0.75f })) stopPlay();
    }
    ui.barSpace(12);
    char status[220], cull[48];
    int sleeping = 0;
    for (auto& b : g.scene.world.bodies) if (b->sleeping) sleeping++;
    if (!g.renderer.frustumCulling)
        snprintf(cull, sizeof(cull), " (culling off, %d out of view)", g.renderer.culledItems);
    else
        snprintf(cull, sizeof(cull), " (%d culled)", g.renderer.culledItems);
    snprintf(status, sizeof(status), "%s | %.0f fps | bodies %d (sleep %d) | contacts %d | meshes %d%s",
             g.mode == Mode::Play ? (g.paused ? "PAUSA" : "PLAY") : "EDIT",
             g.fps, (int)g.scene.world.bodies.size(), sleeping, g.scene.world.contactCount,
             g.renderer.drawnItems, cull);
    ui.barLabel(status, g.mode == Mode::Play ? Vec3{ 1, 0.6f, 0.5f } : Vec3{ 0.55f, 0.59f, 0.66f });
    ui.menuBarEnd();
}

// document tab bar (below the menu bar): [Level name] [Blueprint / Curve tabs]
static void drawDocTabs() {
    UI& ui = g.ui;
    Renderer* r = ui.r;
    const UIInput& in = ui.input();
    const float y = MENUBAR_H, h = DOC_TAB_H;
    const Vec3 barBg = { 0.10f, 0.11f, 0.13f };
    const Vec3 tabOn = { 0.16f, 0.18f, 0.22f };
    const Vec3 tabOff = { 0.115f, 0.125f, 0.15f };
    const Vec3 accent = { 0.30f, 0.62f, 0.99f };
    r->setUIScissor(0, 0, 0, 0, false);
    r->drawRectPx(0, y, (float)g.width, h, barBg, 1);
    r->drawRectPx(0, y + h - 1, (float)g.width, 1, { 0.05f, 0.055f, 0.065f }, 1);
    ui.registerBlockingRect({ 0, y, (float)g.width, h });

    auto tab = [&](const std::string& title, const char* iconName, bool active, bool closable) -> int {
        // returns 0 none, 1 selected, 2 close clicked
        const float iconW = 22.0f;
        const float grip = 12.0f;   // move-grip reserved at the tab's left
        float tw = r->textWidth(title, 1.08f) + iconW + grip + (closable ? 46.0f : 34.0f);
        UIRect trc = { g.docTabX, y + 4, tw, h - 5 };
        bool over = in.mouseX >= trc.x && in.mouseX < trc.x + trc.w && in.mouseY >= trc.y && in.mouseY < trc.y + trc.h;
        r->drawRectPx(trc.x, trc.y, trc.w, trc.h, active ? tabOn : (over ? tabOff * 1.3f : tabOff), 1);
        if (active) r->drawRectPx(trc.x, trc.y, trc.w, 2, accent, 1);
        // drag grip: two short bars, grey by default and accent-blue while hovered —
        // the same handle language used by the dock tabs and the panel splitters.
        Vec3 gripCol = over ? accent : Vec3{ 0.40f, 0.45f, 0.53f };
        float gripA = over ? 0.95f : 0.75f, gy = trc.y + (trc.h - 12) * 0.5f;
        r->drawRectPx(trc.x + 6,  gy, 2, 12, gripCol, gripA);
        r->drawRectPx(trc.x + 10, gy, 2, 12, gripCol, gripA);
        float textX = trc.x + 12 + grip;
        if (iconName) {
            auto it = g.assetIconTextures.find(iconName);
            if (it != g.assetIconTextures.end())
                r->drawImagePx(it->second, trc.x + 11 + grip, trc.y + (trc.h - 17) * 0.5f, 17, 17, { 1, 1, 1 }, 1);
            textX = trc.x + 33 + grip;
        }
        r->drawTextLine(textX, trc.y + 7, title,
                        active ? Vec3{ 0.9f, 0.94f, 1.0f } : Vec3{ 0.6f, 0.66f, 0.74f }, 1, 1.08f);
        int res = 0;
        if (closable) {
            UIRect xr = { trc.x + trc.w - 24, trc.y + 7, 18, 18 };
            bool overX = over && in.mouseX >= xr.x && in.mouseX < xr.x + xr.w;
            r->drawTextLine(xr.x + 4, xr.y, "x", overX ? Vec3{ 1, 0.5f, 0.5f } : Vec3{ 0.6f, 0.66f, 0.74f }, 1, 1.08f);
            if (over && in.mousePressed) res = overX ? 2 : 1;
        } else if (over && in.mousePressed) {
            res = 1;
        }
        g.docTabX += tw + 2;
        return res;
    };

    g.docTabX = 6;
    std::string lvl = g.projectPath[0] ? fs::path(g.projectPath).stem().string() : "Untitled level";
    if (tab(lvl, "level", g.activeDoc == 0, false) == 1) g.activeDoc = 0;
    for (int i = 0; i < (int)g.bpDocs.size(); i++) {
        int res = tab(bpDocTitle(g.bpDocs[i].get()), bpDocIcon(g.bpDocs[i].get()), g.activeDoc == i + 1, true);
        if (res == 2) g.closeDocRequest = i;
        else if (res == 1) g.activeDoc = i + 1;
    }
    for (int i = 0; i < (int)g.curveDocs.size(); i++) {
        int doc = 1 + (int)g.bpDocs.size() + i;
        int res = tab(curveDocTitle(g.curveDocs[i].get()), "curve", g.activeDoc == doc, true);
        if (res == 2) g.closeCurveDocRequest = i;
        else if (res == 1) g.activeDoc = doc;
    }
    for (int i = 0; i < (int)g.materialDocs.size(); i++) {
        int doc = materialDocBase() + i;
        int res = tab(materialDocTitle(g.materialDocs[i].get()), "material", g.activeDoc == doc, true);
        if (res == 2) g.closeMaterialDocRequest = i;
        else if (res == 1) g.activeDoc = doc;
    }
    for (int i = 0; i < (int)g.widgetDocs.size(); i++) {
        int doc = widgetDocBase() + i;
        int res = tab(widgetDocTitle(g.widgetDocs[i].get()), "widget", g.activeDoc == doc, true);
        if (res == 2) g.closeWidgetDocRequest = i;
        else if (res == 1) g.activeDoc = doc;
    }

    // The tab bar carries no Save button: saving lives in each editor's own
    // top-left tool bar (one shared floppy control) and on Ctrl+S.
}

// ── shared Save control: one design across every document editor ──
// Grey while the document is clean, green while there are unsaved changes.
// `dirty` may be null for documents that do not track it (the level scene).
bool drawSaveButton(UI& ui, const UIRect& rc, bool dirty, const char* tooltip) {
    Renderer* r = ui.r;
    const UIInput& in = ui.input();
    bool over = !ui.interactionBlocked() && in.mouseX >= rc.x && in.mouseX < rc.x + rc.w &&
                in.mouseY >= rc.y && in.mouseY < rc.y + rc.h;
    // same palette and glyph proportions as UI::toolIconButton icon 0, so the
    // Save control reads identically in every editor
    Vec3 base = dirty ? Vec3{ 0.13f, 0.33f, 0.21f } : Vec3{ 0.12f, 0.22f, 0.34f };
    Vec3 bg = over ? base * 1.35f : base;
    r->drawRectPx(rc.x, rc.y, rc.w, rc.h, bg, 1);
    Vec3 border = dirty ? Vec3{ 0.35f, 0.85f, 0.5f } : (over ? Vec3{ 0.42f, 0.68f, 0.96f } : Vec3{ 0.25f, 0.29f, 0.36f });
    r->drawRectPx(rc.x, rc.y, rc.w, 1, border, 1);
    r->drawRectPx(rc.x, rc.y + rc.h - 1, rc.w, 1, border, 1);
    r->drawRectPx(rc.x, rc.y, 1, rc.h, border, 1);
    r->drawRectPx(rc.x + rc.w - 1, rc.y, 1, rc.h, border, 1);
    Vec3 fg = dirty ? Vec3{ 0.58f, 0.96f, 0.74f } : Vec3{ 0.72f, 0.78f, 0.87f };
    float cx = rc.x + rc.w * 0.5f, cy = rc.y + rc.h * 0.5f, s = clampf(rc.w / 28.0f, 1.0f, 2.0f);
    r->drawRectPx(cx - 8*s, cy - 8*s, 16*s, 16*s, fg, 1);
    r->drawRectPx(cx - 5*s, cy - 7*s, 8*s, 5*s, bg, 1);
    r->drawRectPx(cx - 5*s, cy + 2*s, 10*s, 5*s, bg, 1);
    if (over && tooltip) ui.hoverTip(tooltip, rc, 0);
    return over && in.mousePressed;
}

// Ctrl+Space content drawers: slide up from the bottom, undockable to floating
static void drawContentDrawers(UI& ui) {
    Renderer* r = ui.r;
    const UIInput& in = ui.input();
    const Vec3 accent = { 0.30f, 0.62f, 0.99f };

    if (g.openDrawerRequest) {
        g.openDrawerRequest = false;
        // Ctrl+Space toggles the bottom drawer: if one is open, close it; else open one.
        // (floating/undocked drawers stay; they are closed via their own 'x')
        ContentDrawer* openBottom = nullptr;
        for (auto& d : g.drawers) if (!d.floating && !d.closing) openBottom = &d;
        if (openBottom) {
            openBottom->closing = true;
        } else {
            ContentDrawer d;
            if (g.hasLastDrawerBrowser) d.st = g.lastDrawerBrowser;
            else { d.st.curRel = ""; d.st.browserTileHeight = g.browserTileHeightPreference; }
            BrowserState primary; browserSave(primary);   // preserve the dock panel's nav
            browserLoad(d.st);
            scanBrowser();                                  // refresh the last visited folder
            browserSave(d.st);
            browserLoad(primary);
            g.drawers.push_back(d);
            addLog(0, "Content drawer (Ctrl+Space to close). 'Dock' to detach it.");
        }
    }

    for (int i = 0; i < (int)g.drawers.size(); i++) {
        ContentDrawer& d = g.drawers[i];
        d.anim += (d.closing ? -0.18f : 0.18f);
        if (d.anim < 0) { g.drawers.erase(g.drawers.begin() + i); i--; continue; }
        if (d.anim > 1) d.anim = 1;

        // layout: bottom sheet or free-floating window
        UIRect rc;
        if (d.floating) {
            rc = d.rect;
            rc.x = clampf(rc.x, -rc.w + 60, (float)g.width - 60);
            rc.y = clampf(rc.y, TOP_H, (float)g.height - 40);
            d.rect = rc;
        } else {
            float bottom = (float)g.height - BOTTOM_BAR_H;   // rest above the status bar
            float maxH = (std::max)(180.0f, bottom - TOP_H - 40.0f);
            float H = d.bottomHeight > 0 ? d.bottomHeight : g.height * 0.42f;
            if (H > 440 && d.bottomHeight <= 0) H = 440;
            H = clampf(H, 160.0f, maxH);
            float slide = H * d.anim;
            rc = { 6, bottom - slide, (float)g.width - 12, H };
            UIRect topGrip = { rc.x, rc.y - 5.0f, rc.w, 10.0f };
            bool overTopGrip = in.mouseX >= topGrip.x && in.mouseX < topGrip.x + topGrip.w &&
                               in.mouseY >= topGrip.y && in.mouseY < topGrip.y + topGrip.h;
            if (overTopGrip && in.mousePressed) d.bottomResizing = true;
            if (d.bottomResizing) {
                d.bottomHeight = clampf(bottom - in.mouseY, 160.0f, maxH);
                H = d.bottomHeight;
                slide = H * d.anim;
                rc = { 6, bottom - slide, (float)g.width - 12, H };
                if (!in.mouseDown) d.bottomResizing = false;
            }
        }
        const float TB = 24;
        r->setUIScissor(0, 0, 0, 0, false);
        r->drawRectPx(rc.x - 1, rc.y - 1, rc.w + 2, rc.h + 2, accent, 0.4f);
        r->drawRectPx(rc.x, rc.y, rc.w, TB, { 0.15f, 0.17f, 0.20f }, 1);
        if (!d.floating) {
            UIRect topGrip = { rc.x, rc.y - 5.0f, rc.w, 10.0f };
            bool overTopGrip = in.mouseX >= topGrip.x && in.mouseX < topGrip.x + topGrip.w &&
                               in.mouseY >= topGrip.y && in.mouseY < topGrip.y + topGrip.h;
            r->drawRectPx(rc.x, rc.y - 2.0f, rc.w, 4.0f, (overTopGrip || d.bottomResizing) ? accent : Vec3{ 0.18f, 0.22f, 0.28f },
                          (overTopGrip || d.bottomResizing) ? 0.95f : 0.42f);
        }
        r->drawTextLine(rc.x + 8, rc.y + 4, d.floating ? "Content (window)" : "Content (drawer)", { 0.85f, 0.9f, 0.97f }, 1);
        ui.registerBlockingRect(rc);

        // buttons: [Sgancia (float) / Aggancia (into dock layout)] [x]
        UIRect dockBtn = { rc.x + rc.w - 150, rc.y + 3, 122, TB - 6 };
        bool overDock = in.mouseX >= dockBtn.x && in.mouseX < dockBtn.x + dockBtn.w && in.mouseY >= dockBtn.y && in.mouseY < dockBtn.y + dockBtn.h;
        r->drawRectPx(dockBtn.x, dockBtn.y, dockBtn.w, dockBtn.h, overDock ? Vec3{ 0.25f, 0.4f, 0.6f } : Vec3{ 0.2f, 0.22f, 0.27f }, 1);
        r->drawTextLine(dockBtn.x + 8, dockBtn.y + 3, d.floating ? "Dock into the layout" : "Undock", { 0.85f, 0.9f, 1.0f }, 1);
        UIRect xBtn = { rc.x + rc.w - 22, rc.y + 4, 16, 16 };
        bool overX = in.mouseX >= xBtn.x && in.mouseX < xBtn.x + xBtn.w && in.mouseY >= xBtn.y && in.mouseY < xBtn.y + xBtn.h;
        r->drawTextLine(xBtn.x + 4, xBtn.y - 1, "x", overX ? Vec3{ 1, 0.5f, 0.5f } : Vec3{ 0.7f, 0.75f, 0.82f }, 1);

        if (in.mousePressed && overDock) {
            if (d.floating) {
                // re-dock the content into the real dock layout so it can be moved,
                // tabbed or floated like any other panel (was previously un-redockable)
                DockWindow* cw = g.dock.find("contenuti");
                if (cw) {
                    cw->open = true;
                    if (cw->area == DOCK_FLOAT || cw->area == DOCK_NATIVE) cw->area = DOCK_BOTTOM;
                    g.dock.setActive("contenuti");
                }
                d.closing = true;
                addLog(0, "Content docked into the layout: drag its tab to move it.");
            } else {
                d.floating = true;
                d.rect = { rc.x + 60, TOP_H + 40, rc.w * 0.7f, rc.h * 0.9f };
            }
        } else if (in.mousePressed && overX) {
            d.closing = true;
        } else if (d.floating) {
            UIRect titleDrag = { rc.x, rc.y, rc.w - 130, TB };
            const float edge = 8.0f;
            bool overLeft = in.mouseX >= rc.x - edge * 0.5f && in.mouseX < rc.x + edge * 0.5f &&
                            in.mouseY >= rc.y && in.mouseY < rc.y + rc.h;
            bool overRight = in.mouseX >= rc.x + rc.w - edge * 0.5f && in.mouseX < rc.x + rc.w + edge * 0.5f &&
                             in.mouseY >= rc.y && in.mouseY < rc.y + rc.h;
            bool overTop = in.mouseY >= rc.y - edge * 0.5f && in.mouseY < rc.y + edge * 0.5f &&
                           in.mouseX >= rc.x && in.mouseX < rc.x + rc.w;
            bool overBottom = in.mouseY >= rc.y + rc.h - edge * 0.5f && in.mouseY < rc.y + rc.h + edge * 0.5f &&
                              in.mouseX >= rc.x && in.mouseX < rc.x + rc.w;
            int hitEdges = (overLeft ? 1 : 0) | (overRight ? 2 : 0) | (overTop ? 4 : 0) | (overBottom ? 8 : 0);
            if (in.mousePressed && hitEdges) {
                d.resizing = true;
                d.resizeEdges = hitEdges;
            } else if (in.mousePressed && in.mouseX >= titleDrag.x && in.mouseX < titleDrag.x + titleDrag.w &&
                       in.mouseY >= titleDrag.y && in.mouseY < titleDrag.y + titleDrag.h) {
                d.dragging = true; d.dragOX = in.mouseX - rc.x; d.dragOY = in.mouseY - rc.y;
            }
            if (d.dragging) { d.rect.x = in.mouseX - d.dragOX; d.rect.y = in.mouseY - d.dragOY; }
            if (d.resizing) {
                const float minW = 320.0f, minH = 160.0f;
                if (d.resizeEdges & 1) {
                    float right = d.rect.x + d.rect.w;
                    float nx = clampf(in.mouseX, 0.0f, right - minW);
                    d.rect.x = nx; d.rect.w = right - nx;
                }
                if (d.resizeEdges & 2) d.rect.w = clampf(in.mouseX - d.rect.x, minW, (float)g.width);
                if (d.resizeEdges & 4) {
                    float bottom = d.rect.y + d.rect.h;
                    float ny = clampf(in.mouseY, TOP_H, bottom - minH);
                    d.rect.y = ny; d.rect.h = bottom - ny;
                }
                if (d.resizeEdges & 8) d.rect.h = clampf(in.mouseY - d.rect.y, minH, (float)g.height);
            }
            if (!in.mouseDown) { d.dragging = false; d.resizing = false; d.resizeEdges = 0; }
            auto edgeCol = [&](int bit, bool hover) { return (d.resizing && (d.resizeEdges & bit)) || hover ? accent : Vec3{ 0.18f, 0.22f, 0.28f }; };
            auto edgeA = [&](int bit, bool hover) { return (d.resizing && (d.resizeEdges & bit)) || hover ? 0.95f : 0.42f; };
            r->drawRectPx(rc.x - 1, rc.y, 3, rc.h, edgeCol(1, overLeft), edgeA(1, overLeft));
            r->drawRectPx(rc.x + rc.w - 2, rc.y, 3, rc.h, edgeCol(2, overRight), edgeA(2, overRight));
            r->drawRectPx(rc.x, rc.y - 1, rc.w, 3, edgeCol(4, overTop), edgeA(4, overTop));
            r->drawRectPx(rc.x, rc.y + rc.h - 2, rc.w, 3, edgeCol(8, overBottom), edgeA(8, overBottom));
        }

        // content: swap the drawer's navigation into the working fields
        BrowserState primary; browserSave(primary);
        browserLoad(d.st);
        char pid[32]; snprintf(pid, sizeof(pid), "drawer%d", i);
        bool drawerBlocked = ui.interactionBlocked();
        if (d.dragging || d.resizing || d.bottomResizing) ui.setInteractionBlocked(true);
        ui.panelBegin(pid, rc.x, rc.y + TB, rc.w, rc.h - TB, nullptr);
        drawContenutiContent(ui);
        ui.panelEnd();
        ui.setInteractionBlocked(drawerBlocked);
        if (d.floating) {
            UIRect frc = d.rect;
            const float edge = 8.0f;
            bool overLeft = in.mouseX >= frc.x - edge * 0.5f && in.mouseX < frc.x + edge * 0.5f &&
                            in.mouseY >= frc.y && in.mouseY < frc.y + frc.h;
            bool overRight = in.mouseX >= frc.x + frc.w - edge * 0.5f && in.mouseX < frc.x + frc.w + edge * 0.5f &&
                             in.mouseY >= frc.y && in.mouseY < frc.y + frc.h;
            bool overTop = in.mouseY >= frc.y - edge * 0.5f && in.mouseY < frc.y + edge * 0.5f &&
                           in.mouseX >= frc.x && in.mouseX < frc.x + frc.w;
            bool overBottom = in.mouseY >= frc.y + frc.h - edge * 0.5f && in.mouseY < frc.y + frc.h + edge * 0.5f &&
                              in.mouseX >= frc.x && in.mouseX < frc.x + frc.w;
            auto edgeCol = [&](int bit, bool hover) { return (d.resizing && (d.resizeEdges & bit)) || hover ? accent : Vec3{ 0.18f, 0.22f, 0.28f }; };
            auto edgeA = [&](int bit, bool hover) { return (d.resizing && (d.resizeEdges & bit)) || hover ? 0.95f : 0.42f; };
            r->setUIScissor(0, 0, 0, 0, false);
            r->drawRectPx(frc.x - 1, frc.y, 3, frc.h, edgeCol(1, overLeft), edgeA(1, overLeft));
            r->drawRectPx(frc.x + frc.w - 2, frc.y, 3, frc.h, edgeCol(2, overRight), edgeA(2, overRight));
            r->drawRectPx(frc.x, frc.y - 1, frc.w, 3, edgeCol(4, overTop), edgeA(4, overTop));
            r->drawRectPx(frc.x, frc.y + frc.h - 2, frc.w, 3, edgeCol(8, overBottom), edgeA(8, overBottom));
        }
        browserSave(d.st);
        g.lastDrawerBrowser = d.st;
        g.hasLastDrawerBrowser = true;
        browserLoad(primary);
    }
}

// ═══ hub screen (project launcher) ═══
static void drawHub() {
    UI& ui = g.ui;
    Renderer* r = ui.r;
    const UIInput& in = ui.input();
    float W = (float)g.width, H = (float)g.height;

    r->drawRectPx(0, 0, W, H, { 0.07f, 0.08f, 0.10f }, 1);
    r->drawRectPx(0, 0, W, 74, { 0.10f, 0.11f, 0.14f }, 1);
    r->drawRectPx(0, 74, W, 2, { 0.05f, 0.055f, 0.065f }, 1);
    r->drawTextLine(40, 18, "PULSE", { 0.30f, 0.62f, 0.99f }, 1, 2.4f);
    r->drawTextLine(44, 48, "Project hub", { 0.6f, 0.66f, 0.74f }, 1);

    float cardW = clampf(W - 220, 520, 940);
    float cx = (W - cardW) * 0.5f;
    float y = 104;

    auto actionBtn = [&](float bx, float bw, const char* label, Vec3 bg) -> bool {
        UIRect rc = { bx, y, bw, 46 };
        bool over = in.mouseX >= rc.x && in.mouseX < rc.x + rc.w && in.mouseY >= rc.y && in.mouseY < rc.y + rc.h;
        r->drawRectPx(rc.x, rc.y, rc.w, rc.h, over ? bg * 1.3f : bg, 1);
        float tw = r->textWidth(label);
        r->drawTextLine(rc.x + (rc.w - tw) * 0.5f, rc.y + 16, label, { 0.92f, 0.95f, 1.0f }, 1);
        return over && in.mousePressed;
    };
    float halfW = (cardW - 12) * 0.5f;
    bool doCreate = actionBtn(cx, halfW, "+  Create new project...", { 0.16f, 0.34f, 0.20f });
    bool doConnect = actionBtn(cx + halfW + 12, halfW, "Add existing project...", { 0.18f, 0.28f, 0.44f });
    y += 62;

    r->drawTextLine(cx, y, "RECENT PROJECTS", { 0.5f, 0.55f, 0.62f }, 1);
    y += 22;

    float listTop = y, listBot = H - 26;
    const float rowH = 56, rowGap = 8;
    if (in.wheel != 0 && in.mouseY > listTop) g.hubScroll += in.wheel * 40;
    float contentH = g.hubProjects.size() * (rowH + rowGap);
    float minScroll = (listBot - listTop) - contentH;
    if (minScroll > 0) minScroll = 0;
    g.hubScroll = clampf(g.hubScroll, minScroll, 0);

    r->setUIScissor(cx, listTop, cardW, listBot - listTop, true);
    if (g.hubProjects.empty()) {
        r->drawTextLine(cx + 6, listTop + 18, "No projects. Create or add a project to get started.",
                        { 0.5f, 0.54f, 0.6f }, 1);
    }
    float ry = listTop + g.hubScroll;
    int openReq = -1, removeReq = -1;
    for (int i = 0; i < (int)g.hubProjects.size(); i++) {
        HubProject& p = g.hubProjects[i];
        UIRect rc = { cx, ry, cardW, rowH };
        ry += rowH + rowGap;
        if (rc.y + rc.h < listTop || rc.y > listBot) continue;
        std::error_code ec;
        bool exists = fs::exists(p.path, ec);
        bool inList = in.mouseY > listTop && in.mouseY < listBot;
        bool over = inList && in.mouseX >= rc.x && in.mouseX < rc.x + rc.w && in.mouseY >= rc.y && in.mouseY < rc.y + rc.h;
        UIRect xr = { rc.x + rc.w - 36, rc.y + rc.h * 0.5f - 13, 26, 26 };
        bool overX = over && in.mouseX >= xr.x && in.mouseX < xr.x + xr.w && in.mouseY >= xr.y && in.mouseY < xr.y + xr.h;
        r->drawRectPx(rc.x, rc.y, rc.w, rc.h, over ? Vec3{ 0.15f, 0.17f, 0.21f } : Vec3{ 0.11f, 0.125f, 0.15f }, 1);
        if (over) r->drawRectPx(rc.x, rc.y, 3, rc.h, { 0.30f, 0.62f, 0.99f }, 1);
        r->drawTextLine(rc.x + 18, rc.y + 9, p.name, exists ? Vec3{ 0.9f, 0.94f, 1.0f } : Vec3{ 0.92f, 0.55f, 0.55f }, 1, 1.2f);
        std::string sub = ui.ellipsize(exists ? p.path : (p.path + "   (folder not found)"), cardW - 70);
        r->drawTextLine(rc.x + 18, rc.y + 32, sub, { 0.55f, 0.6f, 0.68f }, 1, 0.9f);
        r->drawRectPx(xr.x, xr.y, xr.w, xr.h, overX ? Vec3{ 0.4f, 0.16f, 0.16f } : Vec3{ 0.16f, 0.17f, 0.2f }, 1);
        r->drawTextLine(xr.x + 9, xr.y + 5, "x", overX ? Vec3{ 1, 0.7f, 0.7f } : Vec3{ 0.6f, 0.65f, 0.72f }, 1);
        if (in.mousePressed && overX) removeReq = i;
        else if (in.mousePressed && over && exists) openReq = i;
    }
    r->setUIScissor(0, 0, 0, 0, false);

    // defer actions until after the layout pass (they mutate g.hubProjects)
    if (doCreate) hubCreateProject();
    else if (doConnect) hubConnectProject();
    else if (openReq >= 0) { std::string pth = g.hubProjects[openReq].path, ll = g.hubProjects[openReq].lastLevel; openProjectAt(pth, ll); }
    else if (removeReq >= 0) hubRemove(removeReq);
}

// ═══ viewport toolbar (transform tools + debug toggles) ═══
static void drawViewportToolbar() {
    UI& ui = g.ui;
    Renderer* r = ui.r;
    const UIInput& in = ui.input();
    UIRect vp = viewportRect();
    if (vp.w < 60 || vp.h < 40) return;
    const float H = 30;
    float x = vp.x, y = vp.y;
    r->setUIScissor(0, 0, 0, 0, false);
    r->drawRectPx(x, y, vp.w, H, { 0.11f, 0.125f, 0.15f }, 0.97f);
    r->drawRectPx(x, y + H, vp.w, 1, { 0.05f, 0.055f, 0.065f }, 1);
    ui.registerBlockingRect({ x, y, vp.w, H });

    float bx = x + 6;
    const char* hoveredTip = nullptr;
    UIRect hoveredRect{};
    auto iconBtn = [&](int icon, const char* tip, bool active) -> bool {
        UIRect rc = { bx, y + 3, 27, H - 6 };
        bx += rc.w + 3;
        bool over = in.mouseX >= rc.x && in.mouseX < rc.x + rc.w && in.mouseY >= rc.y && in.mouseY < rc.y + rc.h;
        Vec3 bg = active ? Vec3{ 0.20f, 0.42f, 0.66f } : (over ? Vec3{ 0.22f, 0.24f, 0.29f } : Vec3{ 0.15f, 0.16f, 0.19f });
        r->drawRectPx(rc.x, rc.y, rc.w, rc.h, bg, 1);
        Vec3 c = active ? Vec3{ 0.96f, 0.98f, 1.0f } : Vec3{ 0.72f, 0.77f, 0.84f };
        float cx = rc.x + rc.w * 0.5f, cy = rc.y + rc.h * 0.5f;
        auto line = [&](float x0, float y0, float x1, float y1, float thick = 1.5f) {
            r->drawLinePx(x0, y0, x1, y1, thick, c, 1);
        };
        if (icon == 0) { // move: two crossed arrows
            line(cx - 8, cy, cx + 8, cy); line(cx, cy - 8, cx, cy + 8);
            r->drawTriPx(cx + 8, cy, cx + 4, cy - 3, cx + 4, cy + 3, c, 1);
            r->drawTriPx(cx - 8, cy, cx - 4, cy - 3, cx - 4, cy + 3, c, 1);
            r->drawTriPx(cx, cy - 8, cx - 3, cy - 4, cx + 3, cy - 4, c, 1);
            r->drawTriPx(cx, cy + 8, cx - 3, cy + 4, cx + 3, cy + 4, c, 1);
        } else if (icon == 1) { // rotate
            const float pxs[] = { -7,-7,-5,-2,2,5,7,7,5 };
            const float pys[] = {  1,-3,-6,-8,-8,-6,-3,1,4 };
            for (int i = 0; i < 8; i++) line(cx + pxs[i], cy + pys[i], cx + pxs[i + 1], cy + pys[i + 1]);
            r->drawTriPx(cx + 5, cy + 4, cx + 9, cy + 4, cx + 7, cy + 8, c, 1);
        } else if (icon == 2) { // scale
            line(cx - 7, cy + 7, cx + 7, cy - 7, 2);
            r->drawRectPx(cx + 4, cy - 9, 5, 5, c, 1);
            r->drawRectPx(cx - 9, cy + 4, 5, 5, c, 1);
        } else if (icon == 3) { // gizmo visibility / eye
            line(cx - 9, cy, cx - 4, cy - 5); line(cx - 4, cy - 5, cx + 4, cy - 5);
            line(cx + 4, cy - 5, cx + 9, cy); line(cx + 9, cy, cx + 4, cy + 5);
            line(cx + 4, cy + 5, cx - 4, cy + 5); line(cx - 4, cy + 5, cx - 9, cy);
            r->drawRectPx(cx - 2, cy - 2, 4, 4, c, 1);
        } else if (icon == 4) { // grid
            for (int i = -1; i <= 1; i++) {
                line(cx - 8, cy + i * 5, cx + 8, cy + i * 5, 1);
                line(cx + i * 5, cy - 8, cx + i * 5, cy + 8, 1);
            }
        } else if (icon == 5) { // collider wire cube
            line(cx - 7, cy - 5, cx + 3, cy - 5); line(cx + 3, cy - 5, cx + 7, cy - 1);
            line(cx + 7, cy - 1, cx + 7, cy + 7); line(cx + 7, cy + 7, cx - 3, cy + 7);
            line(cx - 3, cy + 7, cx - 7, cy + 3); line(cx - 7, cy + 3, cx - 7, cy - 5);
            line(cx - 3, cy - 1, cx + 7, cy - 1); line(cx - 3, cy - 1, cx - 3, cy + 7);
            line(cx - 7, cy - 5, cx - 3, cy - 1);
        } else if (icon == 7) { // world/local transform space
            const char* space = g.gizmoLocal ? "L" : "W";
            float tw = r->textWidth(space, 1.08f);
            r->drawTextLine(cx-tw*.5f,cy-r->fontHeight()*.54f,space,c,1,1.08f);
        } else if (icon == 9) { // frustum culling: camera cone with a clipped box
            line(cx - 8, cy, cx + 6, cy - 8); line(cx - 8, cy, cx + 6, cy + 8);
            line(cx + 6, cy - 8, cx + 6, cy + 8);
            r->drawRectPx(cx - 1, cy - 3, 6, 6, c, 1);          // inside: kept
            r->drawRectPx(cx + 8, cy - 9, 4, 4, c, 0.32f);      // outside: culled
        } else if(icon==8){ // magnet
            r->drawLinePx(cx-7,cy-7,cx-7,cy+2,3,c,1);
            r->drawLinePx(cx+7,cy-7,cx+7,cy+2,3,c,1);
            r->drawLinePx(cx-7,cy+2,cx-4,cy+7,3,c,1);
            r->drawLinePx(cx+7,cy+2,cx+4,cy+7,3,c,1);
            r->drawLinePx(cx-4,cy+7,cx+4,cy+7,3,c,1);
            r->drawRectPx(cx-9,cy-9,4,4,{1,.22f,.18f},1);
            r->drawRectPx(cx+5,cy-9,4,4,{.18f,.48f,1},1);
        } else { // contact points
            line(cx - 8, cy + 5, cx + 8, cy - 5, 1);
            r->drawRectPx(cx - 8, cy - 7, 5, 5, c, 1);
            r->drawRectPx(cx - 2, cy - 2, 5, 5, c, 1);
            r->drawRectPx(cx + 4, cy + 3, 5, 5, c, 1);
        }
        if (over) { hoveredTip = tip; hoveredRect = rc; }
        return over && in.mousePressed;
    };
    auto sep = [&]() { r->drawRectPx(bx + 2, y + 5, 1, H - 10, { 0.3f, 0.32f, 0.36f }, 1); bx += 8; };

    if (iconBtn(0, "Move (W)", g.gizmoMode == 0)) g.gizmoMode = 0;
    if (iconBtn(1, "Rotate (Q)", g.gizmoMode == 1)) g.gizmoMode = 1;
    if (iconBtn(2, "Scala (E)", g.gizmoMode == 2)) g.gizmoMode = 2;
    if (iconBtn(7, g.gizmoLocal ? "Local space" : "World space", g.gizmoLocal)) g.gizmoLocal = !g.gizmoLocal;
    sep();
    if (iconBtn(3, "Show gizmo", g.showGizmo)) g.showGizmo = !g.showGizmo;
    sep();
    if(iconBtn(8,g.transformSnap?"Snap on":"Snap off",g.transformSnap))g.transformSnap=!g.transformSnap;
    auto snapValue=[&](const char* prefix,float& value,const float* values,int count,const char* suffix){
        char text[32];snprintf(text,sizeof(text),"%s %.3g%s",prefix,value,suffix);
        float w=r->textWidth(text,.88f)+12;UIRect rc{bx,y+3,w,H-6};bx+=w+3;
        bool over=in.mouseX>=rc.x&&in.mouseX<rc.x+rc.w&&in.mouseY>=rc.y&&in.mouseY<rc.y+rc.h;
        r->drawRectPx(rc.x,rc.y,rc.w,rc.h,over?Vec3{.23f,.26f,.32f}:Vec3{.145f,.155f,.185f},1);
        r->drawTextLine(rc.x+6,rc.y+5,text,g.transformSnap?Vec3{.86f,.92f,1}:Vec3{.56f,.60f,.68f},1,.88f);
        if(over){hoveredTip="Click: change snap value";hoveredRect=rc;}
        if(over&&in.mousePressed){int nearest=0;float error=1e30f;for(int i=0;i<count;i++){float e=fabsf(values[i]-value);if(e<error){error=e;nearest=i;}}value=values[(nearest+1)%count];}
    };
    static const float MOVE_VALUES[]={.1f,.5f,1,5,10,50,100};
    static const float ROT_VALUES[]={1,5,10,15,30,45,90};
    static const float SCALE_VALUES[]={.01f,.05f,.1f,.25f,.5f,1};
    snapValue("M",g.moveSnap,MOVE_VALUES,7,"");
    snapValue("R",g.rotateSnap,ROT_VALUES,7," deg");
    snapValue("S",g.scaleSnap,SCALE_VALUES,6,"");
    sep();
    if (iconBtn(4, "Show grid", g.showGrid)) g.showGrid = !g.showGrid;
    if (iconBtn(5, "Show colliders", g.showColliders)) g.showColliders = !g.showColliders;
    if (iconBtn(6, "Show contact points", g.showContacts)) g.showContacts = !g.showContacts;
    {
        char tip[96];
        snprintf(tip, sizeof(tip), g.renderer.frustumCulling
                     ? "Frustum culling ON - %d meshes drawn, %d culled"
                     : "Frustum culling OFF - %d meshes drawn (%d cullable)",
                 g.renderer.drawnItems, g.renderer.culledItems);
        if (iconBtn(9, tip, g.renderer.frustumCulling)) {
            g.renderer.frustumCulling = !g.renderer.frustumCulling;
            saveEditorPreferences();
        }
    }

    if(g.prefabEditMode){
        sep();
        std::string title="PREFAB  "+fs::path(g.prefabEditRel).stem().string();
        r->drawTextLine(bx,y+7,title,{.48f,.74f,1.0f},1);bx+=r->textWidth(title)+10;
        auto prefabButton=[&](const char* label,Vec3 color){
            float w=r->textWidth(label)+18;UIRect rc{bx,y+3,w,H-6};bx+=w+4;
            bool over=in.mouseX>=rc.x&&in.mouseX<rc.x+rc.w&&in.mouseY>=rc.y&&in.mouseY<rc.y+rc.h;
            r->drawRectPx(rc.x,rc.y,rc.w,rc.h,over?color*1.25f:color,1);
            r->drawTextLine(rc.x+9,rc.y+4,label,{.94f,.97f,1},1);return over&&in.mousePressed;
        };
        if(prefabButton("Save",{.10f,.32f,.22f}))savePrefabEdit();
        if(prefabButton("Save and exit",{.12f,.28f,.48f}))closePrefabEdit(true);
        if(prefabButton("Quit",{.32f,.16f,.16f}))closePrefabEdit(false);
    }

    if(g.gizmoAxis>=0&&g.gizmoMode==1){
        char delta[48];snprintf(delta,sizeof(delta),"%+.1f deg",g.gizmoRotationDeltaDeg);
        float tw=r->textWidth(delta);float px=clampf((float)g.mouseX+18,vp.x+4,vp.x+vp.w-tw-20);
        float py=clampf((float)g.mouseY+18,vp.y+H+5,vp.y+vp.h-29);
        r->drawRectPx(px+3,py+3,tw+16,24,{0,0,0},.38f);
        r->drawRectPx(px,py,tw+16,24,{.08f,.09f,.11f},.98f);
        r->drawRectPx(px,py,3,24,{1,.78f,.08f},1);
        r->drawTextLine(px+9,py+4,delta,{1,.93f,.62f},1);
    }

    if (hoveredTip) {
        float tw = r->textWidth(hoveredTip);
        float tx = hoveredRect.x;
        if (tx + tw + 14 > vp.x + vp.w) tx = vp.x + vp.w - tw - 14;
        float ty = y + H + 4;
        r->drawRectPx(tx + 2, ty + 2, tw + 14, 23, { 0, 0, 0 }, 0.35f);
        r->drawRectPx(tx, ty, tw + 14, 23, { 0.10f, 0.11f, 0.13f }, 0.98f);
        r->drawTextLine(tx + 7, ty + 4, hoveredTip, { 0.9f, 0.92f, 0.96f }, 1);
    }
}

static bool drawerCoversPointerThisFrame() {
    float mx = g.uiIn.mouseX, my = g.uiIn.mouseY;
    for (const ContentDrawer& d : g.drawers) {
        float nextAnim = d.anim + (d.closing ? -0.18f : 0.18f);
        if (nextAnim < 0) continue;
        nextAnim = clampf(nextAnim, 0, 1);
        UIRect rc;
        if (d.floating) {
            rc = d.rect;
            rc.x = clampf(rc.x, -rc.w + 60, (float)g.width - 60);
            rc.y = clampf(rc.y, TOP_H, (float)g.height - 40);
        } else {
            float bottom = (float)g.height - BOTTOM_BAR_H;
            float maxH = (std::max)(180.0f, bottom - TOP_H - 40.0f);
            float h = d.bottomHeight > 0 ? d.bottomHeight : g.height * 0.42f;
            if (h > 440 && d.bottomHeight <= 0) h = 440;
            h = clampf(h, 160.0f, maxH);
            rc = { 6, bottom - h * nextAnim, (float)g.width - 12, h };
        }
        if (mx >= rc.x && mx < rc.x + rc.w && my >= rc.y && my < rc.y + rc.h) return true;
    }
    return false;
}

static bool dismissBottomDrawerOnOutsideClick() {
    if (!g.uiIn.mousePressed) return false;
    // a press on the bottom status bar (which owns the drawer toggle button) is not
    // an "outside" click — the bar handles opening/closing itself
    if (g.uiIn.mouseY >= (float)g.height - BOTTOM_BAR_H) return false;
    bool dismissed = false;
    for (ContentDrawer& d : g.drawers) {
        if (d.floating || d.closing || d.anim <= 0.0f) continue;
        float bottom = (float)g.height - BOTTOM_BAR_H;
        float maxH = (std::max)(180.0f, bottom - TOP_H - 40.0f);
        float h = d.bottomHeight > 0 ? d.bottomHeight : g.height * 0.42f;
        if (h > 440 && d.bottomHeight <= 0) h = 440;
        h = clampf(h, 160.0f, maxH);
        UIRect rc = { 6, bottom - h * clampf(d.anim, 0, 1), (float)g.width - 12, h };
        bool inside = g.uiIn.mouseX >= rc.x && g.uiIn.mouseX < rc.x + rc.w &&
                      g.uiIn.mouseY >= rc.y && g.uiIn.mouseY < rc.y + rc.h;
        if (!inside) {
            d.closing = true;
            dismissed = true;
        }
    }
    return dismissed;
}

// in Play, draw every widget currently on the viewport (the level's HUD widget
// is one of them — startPlay creates it like any Create Widget instance)
static void drawPlayHUD() {
    if (g.mode != Mode::Play || g.activeDoc != 0) return;
    if (g.runtimeWidgets.empty()) return;
    UIRect vp = playWidgetRect();
    g.renderer.setUIScissor(0, 0, 0, 0, false);
    for (auto& rw : g.runtimeWidgets)
        if (rw->visible) widgetRenderTree(g.ui, rw->asset, vp, &g.renderer, g.projectDir, -1);
    // Tool Tip Text of whatever the pointer rests on (only while the cursor is
    // free — with a captured FPS mouse there is nothing to hover)
    if (!g.playMouseCaptured) {
        for (auto& rw : g.runtimeWidgets) {
            if (!rw->visible) continue;
            const WidgetNode* hit = widgetNodeAtPoint(rw->asset, vp, g.uiIn.mouseX, g.uiIn.mouseY);
            if (hit && hit->tooltip[0]) { g.ui.showTip(hit->tooltip); break; }
        }
    }
}

// ── OS text clipboard (for Ctrl+C / Ctrl+V inside focused text fields) ──
static std::string getClipboardText() {
    if (!OpenClipboard(g.hwnd)) return "";
    std::string out;
    if (HANDLE h = GetClipboardData(CF_TEXT)) {
        if (const char* p = (const char*)GlobalLock(h)) { out = p; GlobalUnlock(h); }
    }
    CloseClipboard();
    return out;
}
static void setClipboardText(const std::string& s) {
    if (!OpenClipboard(g.hwnd)) return;
    EmptyClipboard();
    if (HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE, s.size() + 1)) {
        if (char* p = (char*)GlobalLock(h)) { memcpy(p, s.c_str(), s.size() + 1); GlobalUnlock(h); SetClipboardData(CF_TEXT, h); }
    }
    CloseClipboard();
}

// Thin Unreal-style status bar pinned to the very bottom. Its left button summons
// the content drawer (same as Ctrl+Space). The bottom drawer rests just above it.
static void drawBottomBar(UI& ui) {
    Renderer* r = ui.r;
    const UIInput& in = ui.input();          // block already cleared before this call
    float y = (float)g.height - BOTTOM_BAR_H, w = (float)g.width;
    r->setUIScissor(0, 0, 0, 0, false);
    r->drawRectPx(0, y, w, BOTTOM_BAR_H, { 0.115f, 0.125f, 0.15f }, 1);
    r->drawRectPx(0, y, w, 1, { 0.28f, 0.32f, 0.40f }, 0.75f);   // top separator

    bool drawerOpen = false;
    for (const ContentDrawer& d : g.drawers) if (!d.floating && !d.closing) drawerOpen = true;

    const char* drawerLabel = drawerOpen ? "v  Content Drawer" : "^  Content Drawer";
    const float pad = 11;
    UIRect btn = { 6, y + 4, r->textWidth(drawerLabel) + pad * 2, BOTTOM_BAR_H - 8 };
    bool hover = in.mouseX >= btn.x && in.mouseX < btn.x + btn.w && in.mouseY >= btn.y && in.mouseY < btn.y + btn.h;
    Vec3 bg = drawerOpen ? Vec3{ 0.20f, 0.34f, 0.52f } : (hover ? Vec3{ 0.20f, 0.23f, 0.28f } : Vec3{ 0.155f, 0.175f, 0.21f });
    r->drawRectPx(btn.x, btn.y, btn.w, btn.h, bg, 1);
    r->drawTextLine(btn.x + pad, btn.y + (btn.h - r->fontHeight()) * 0.5f, drawerLabel, { 0.86f, 0.91f, 0.98f }, 1);
    if (hover && in.mouseReleased) g.openDrawerRequest = true;

    ui.registerBlockingRect({ 0, y, w, BOTTOM_BAR_H });
}

static void drawEditorUI() {
    if (g.uiIn.keyPaste) g.ui.setPasteText(getClipboardText());   // feed the OS clipboard to fields
    g.ui.begin(&g.renderer, g.uiIn);
    // Register asset icons up front, every frame: the outliner (and any panel that
    // happens to draw before the content browser, or when the browser tab is hidden)
    // needs them available immediately on project open, not only after Contenuti runs.
    for (const auto& icon : g.assetIconTextures) g.ui.setAssetIcon(icon.first, icon.second);
    if (g.standaloneMode || g.playFullscreenActive) {
        drawMinimalPlayBar();
        g.ui.end();
        return;
    }
    UIRect buildRc = buildWindowRect();
    bool buildBlocksPointer = g.buildWindowOpen &&
        g.uiIn.mouseX >= buildRc.x && g.uiIn.mouseX < buildRc.x + buildRc.w &&
        g.uiIn.mouseY >= buildRc.y && g.uiIn.mouseY < buildRc.y + buildRc.h;
    UIRect settingsRc = settingsWindowRect();
    bool settingsBlocksPointer = g.settingsWindowOpen &&
        g.uiIn.mouseX >= settingsRc.x && g.uiIn.mouseX < settingsRc.x + settingsRc.w &&
        g.uiIn.mouseY >= settingsRc.y && g.uiIn.mouseY < settingsRc.y + settingsRc.h;
    bool modalBlocksPointer = buildBlocksPointer || settingsBlocksPointer;
    bool drawerDismissed = dismissBottomDrawerOnOutsideClick();
    // Drawers are rendered after the document, but they own pointer input in
    // their visible rectangle. Pre-block the document to avoid two RMB menus.
    // Preserve captures started by the drawer on the previous frame: clearing
    // activeId here made every tile/folder release disappear before the drawer
    // itself was drawn, so nothing inside it could ever be selected or opened.
    g.ui.setInteractionBlocked(drawerCoversPointerThisFrame() || drawerDismissed || modalBlocksPointer, false);
    int contentH = g.height - (int)BOTTOM_BAR_H;   // leave room for the bottom status bar
    if (g.activeDoc == 0) {
        g.dock.drawAll(g.ui, g.width, contentH, TOP_H, windowContent);
        // hide the viewport command bar while a tab is being dragged, so the dragged
        // tab is never covered by it (the bar draws on top of the dock's windows)
        if (!g.dock.draggingWindow()) drawViewportToolbar();
        drawPlayHUD();   // the level's UI widget, on top of the game view in Play
    } else {
        // One panel id per open document, never one per document *kind*: the
        // panel's scroll offset (and its scrollbar) lives in UI storage keyed by
        // that id, so a shared id made every Blueprint tab scroll as one.
        char docPanelId[32];
        snprintf(docPanelId, sizeof(docPanelId), "doc%d", g.activeDoc);
        if (BPEditor* bp = activeBP()) {
            g.ui.panelBegin(docPanelId, 0, TOP_H, (float)g.width, contentH - TOP_H, nullptr);
            bp->draw(g.ui);
            g.ui.panelEnd();
        } else if (CurveEditor* curve = activeCurve()) {
            g.ui.panelBegin(docPanelId, 0, TOP_H, (float)g.width, contentH - TOP_H, nullptr);
            curve->draw(g.ui);
            g.ui.panelEnd();
        } else if (MaterialEditor* mat = activeMaterial()) {
            g.ui.panelBegin(docPanelId, 0, TOP_H, (float)g.width, contentH - TOP_H, nullptr);
            mat->draw(g.ui);
            g.ui.panelEnd();
        } else if (WidgetEditor* wid = activeWidget()) {
            g.ui.panelBegin(docPanelId, 0, TOP_H, (float)g.width, contentH - TOP_H, nullptr);
            wid->draw(g.ui);
            g.ui.panelEnd();
        }
    }
    g.ui.setInteractionBlocked(modalBlocksPointer);
    drawContentDrawers(g.ui);   // Ctrl+Space drawers, on top of the level/blueprint
    g.ui.setInteractionBlocked(false);
    drawBottomBar(g.ui);        // thin status bar with the Content Drawer button
    drawDocTabs();
    drawMenuBar();
    drawBuildWindow();
    drawSettingsWindow();
    if (g.activeDoc == 0) g.dock.drawDragOverlay(g.ui);
    g.ui.end();
    { std::string copied; if (g.ui.takeCopyText(copied)) setClipboardText(copied); }   // Ctrl+C from a field → OS clipboard

    // deferred: close a blueprint tab
    if (g.closeDocRequest >= 0) {
        int i = g.closeDocRequest;
        g.closeDocRequest = -1;
        if (i < (int)g.bpDocs.size()) {
            g.bpDocs.erase(g.bpDocs.begin() + i);
            if (g.activeDoc - 1 == i) g.activeDoc = 0;
            else if (g.activeDoc - 1 > i) g.activeDoc--;
        }
    }
    if (g.closeCurveDocRequest >= 0) {
        int i = g.closeCurveDocRequest;
        g.closeCurveDocRequest = -1;
        int doc = 1 + (int)g.bpDocs.size() + i;
        if (i < (int)g.curveDocs.size()) {
            if (g.curveDocs[i]) g.curveCache.erase(g.curveDocs[i]->curPath);
            g.curveDocs.erase(g.curveDocs.begin() + i);
            if (g.activeDoc == doc) g.activeDoc = 0;
            else if (g.activeDoc > doc) g.activeDoc--;
        }
    }
    if (g.closeMaterialDocRequest >= 0) {
        int i = g.closeMaterialDocRequest;
        g.closeMaterialDocRequest = -1;
        int doc = materialDocBase() + i;
        if (i < (int)g.materialDocs.size()) {
            g.materialDocs.erase(g.materialDocs.begin() + i);
            g.materialCache.clear();   // reload assigned materials from disk after editing
            if (g.activeDoc == doc) g.activeDoc = 0;
            else if (g.activeDoc > doc) g.activeDoc--;
        }
    }
    if (g.closeWidgetDocRequest >= 0) {
        int i = g.closeWidgetDocRequest;
        g.closeWidgetDocRequest = -1;
        int doc = widgetDocBase() + i;
        if (i < (int)g.widgetDocs.size()) {
            g.widgetDocs.erase(g.widgetDocs.begin() + i);
            if (g.activeDoc == doc) g.activeDoc = 0;
            else if (g.activeDoc > doc) g.activeDoc--;
        }
    }
}

// ═══ window / input ═══
static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_CLOSE:
    case WM_DESTROY:
        g.running = false;
        PostQuitMessage(0);
        return 0;
    case WM_SIZE:
        g.width = LOWORD(lp) > 1 ? LOWORD(lp) : 1;
        g.height = HIWORD(lp) > 1 ? HIWORD(lp) : 1;
        g.renderer.resize(g.width, g.height);
        return 0;
    case WM_LBUTTONDOWN:
        g.mouseX = GET_X_LPARAM(lp); g.mouseY = GET_Y_LPARAM(lp);
        g.uiIn.mouseX = (float)g.mouseX;
        g.uiIn.mouseY = (float)g.mouseY;
        g.uiIn.mousePressed = true;
        g.uiIn.mouseDown = true;
        g.viewportFocused = !g.inHub && g.activeDoc == 0 && mouseInViewport() && !g.ui.wantMouse();
        g.outlinerFocused = !g.inHub && g.activeDoc == 0 && g.dock.windowHovered("outliner", g.uiIn.mouseX, g.uiIn.mouseY);
        if (!g.inHub && g.activeDoc == 0 && g.dock.windowHovered("animation", g.uiIn.mouseX, g.uiIn.mouseY))
            g.animationFocused = true;
        else
            g.animationFocused = false;
        // In Play with the mouse released (Shift+F1), a click in the viewport
        // re-grabs it — unless it lands on a widget, which owns that click.
        if (!g.inHub && !g.flyActive) {
            if (g.mode == Mode::Play && !g.playMouseCaptured && mouseInViewport()) {
                if (!playWidgetUnderCursor()) capturePlayMouse();
            }
            else if (!g.ui.wantMouse() && mouseInViewport()) viewportMouseDown();
        }
        SetCapture(hwnd);
        return 0;
    case WM_LBUTTONUP:
        g.uiIn.mouseReleased = true;
        g.uiIn.mouseDown = false;
        g.gizmoAxis = -1;
        g.grabBody = nullptr;
        if (!g.orbiting && !g.panning) ReleaseCapture();
        return 0;
    case WM_RBUTTONDOWN:
        g.uiIn.rmbPressed = true;
        g.uiIn.rmbDown = true;
        if (!g.inHub && !g.ui.wantMouse() && mouseInViewport()) {
            // Play: RMB = free-fly look (no game camera, or paused "eject"); Edit: orbit
            if (g.mode == Mode::Play) {
                if (!sceneHasCamera() || g.paused) {
                    beginFreeLookFromPlayView();
                    g.flyLook = true;
                    SetCapture(hwnd);
                }
            } else {
                g.orbiting = true;
                SetCapture(hwnd);
            }
        }
        return 0;
    case WM_RBUTTONUP:
        g.uiIn.rmbReleased = true;
        g.uiIn.rmbDown = false;
        g.orbiting = false;
        g.flyLook = false;
        if (!g.panning && !g.uiIn.mouseDown) ReleaseCapture();
        return 0;
    case WM_MBUTTONDOWN:
        g.uiIn.mmbPressed = true;
        g.uiIn.mmbDown = true;
        if (!g.inHub && !g.ui.wantMouse() && mouseInViewport()) g.panning = true;
        SetCapture(hwnd);
        return 0;
    case WM_MBUTTONUP:
        g.uiIn.mmbReleased = true;
        g.uiIn.mmbDown = false;
        g.panning = false;
        if (!g.orbiting && !g.uiIn.mouseDown) ReleaseCapture();
        return 0;
    case WM_MOUSEMOVE: {
        int x = GET_X_LPARAM(lp), y = GET_Y_LPARAM(lp);
        int dx = x - g.mouseX, dy = y - g.mouseY;
        g.mouseX = x; g.mouseY = y;
        g.uiIn.mouseX = (float)x;
        g.uiIn.mouseY = (float)y;
        // Play mouse-look deltas are polled+recentred in the frame loop (see updatePlayMouse)
        if (!g.inHub) {
            if (g.flyLook) {   // assi standard: destra→destra, su→su
                g.flyYaw -= (float)dx * 0.005f;
                g.flyPitch = clampf(g.flyPitch - (float)dy * 0.005f, -1.5f, 1.5f);
            } else if (g.orbiting) g.camera.freeLook((float)dx, (float)dy);
            else if (g.panning) g.camera.pan((float)dx, (float)dy);
            if (!g.flyActive) viewportMouseMove();
        }
        return 0;
    }
    case WM_MOUSEWHEEL: {
        float notches = (float)GET_WHEEL_DELTA_WPARAM(wp) / WHEEL_DELTA;
        if (g.inHub) { g.uiIn.wheel += notches; return 0; }
        if (g.mode == Mode::Play && !g.ui.wantMouse()) g.bpWheelAccum += notches;
        if (g.ui.wantMouse()) g.uiIn.wheel += notches;
        else g.camera.zoom(-notches * 120);
        return 0;
    }
    case WM_CHAR:
        if ((g.ui.wantKeyboard() || (activeBP() && activeBP()->wantsTextInput()) ||
             (activeWidget() && activeWidget()->wantsTextInput())) && g.uiIn.typedCount < 31) {
            char c = (char)wp;
            if (c >= 32 && c < 127) g.uiIn.typed[g.uiIn.typedCount++] = c;
        }
        return 0;
    case WM_KEYUP:
        for (int i = 0; i < BP_NKEYS; i++) {
            if ((int)wp == BP_KEY_VKS[i]) {
                if (g.bpKeysDown[i] && g.mode == Mode::Play) g.bpKeyReleases.push_back(i);
                g.bpKeysDown[i] = false;
            }
        }
        return 0;
    case WM_KEYDOWN: {
        if (wp == VK_F11 && !(lp & (1 << 30))) {
            if (g.playFullscreenActive || g.standaloneMode) setWindowFullscreen(!g.windowFullscreen);
            else toggleEditorFullscreen();
            return 0;
        }
        if (g.inHub) return 0;   // the hub uses only mouse; ignore editor shortcuts
        // Shift+F1 releases the Play mouse capture (mouse look stops, cursor freed)
        if (wp == VK_F1 && (GetKeyState(VK_SHIFT) & 0x8000)) {
            if (g.mode == Mode::Play) releasePlayMouse();
            return 0;
        }
        // Ctrl+Space opens a content drawer anywhere (level or blueprint)
        if (wp == VK_SPACE && (GetKeyState(VK_CONTROL) & 0x8000)) {
            g.openDrawerRequest = true;
            return 0;
        }
        // A widget in Graph mode behaves like a Blueprint tab for shortcuts.
        BPEditor* bp = activeBP();
        if (!bp) { WidgetEditor* wd = activeWidget(); if (wd && wd->graphMode) bp = &wd->graph; }
        // "listen for key" del blueprint: il prossimo tasto va al binding, non alle shortcut
        if (bp && bp->listeningKey()) {
            if (wp == VK_ESCAPE) g.uiIn.keyEscape = true;
            else g.uiIn.keyPressedVK = (int)wp;
            return 0;
        }
        if ((GetKeyState(VK_CONTROL) & 0x8000) && !(lp & (1 << 30))) {
            if (wp == 'S') {
                // Ctrl+Shift+S = save the whole project; Ctrl+S = save the active asset/scene
                if (GetKeyState(VK_SHIFT) & 0x8000) saveAllProject();
                else saveActiveDoc();
                return 0;
            }
            if (wp == 'Z') { if (bp) bp->undo(); else undoScene(); return 0; }
            if (wp == 'X') { if (bp) bp->redo(); else redoScene(); return 0; }
        }
        if (g.ui.wantKeyboard() || (bp && bp->wantsTextInput()) ||
            (activeWidget() && activeWidget()->wantsTextInput())) {
            bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
            if (wp == VK_BACK) g.uiIn.keyBackspace = true;
            if (wp == VK_DELETE) g.uiIn.keyDelete = true;
            if (wp == VK_LEFT) g.uiIn.keyLeft = true;
            if (wp == VK_RIGHT) g.uiIn.keyRight = true;
            if (wp == 'A' && ctrl) g.uiIn.keySelectAll = true;
            if (wp == 'C' && ctrl) g.uiIn.keyCopy = true;   // copy selected text in the focused field
            if (wp == 'V' && ctrl) g.uiIn.keyPaste = true;  // paste OS clipboard into the field
            if (wp == VK_RETURN) g.uiIn.keyEnter = true;
            if (wp == VK_ESCAPE) g.uiIn.keyEscape = true;
            return 0;
        }
        if (g.mode == Mode::Edit && g.animationFocused && g.activeDoc == 0 &&
            wp == 'S' && !(lp & (1 << 30))) {
            addOrReplaceAnimationKey();
            return 0;
        }
        // N toggles the Navigation/path overlay whenever the main editor is
        // active — no need to focus the viewport or select an object first.
        if (g.mode == Mode::Edit && g.activeDoc == 0 && !g.orbiting && wp == 'N' && !(lp & (1 << 30))) {
            g.navigation.show = !g.navigation.show;
            saveEditorPreferences();
            addLog(0, g.navigation.show ? "Navigation/path display on (N)."
                                        : "Navigation/path display hidden (N).");
            return 0;
        }
        // while right-dragging the viewport, W/A/S/D/Q/E fly the camera (see
        // updateEditFly), so they must not also toggle the gizmo tool
        if (g.mode == Mode::Edit && g.activeDoc == 0 && !g.orbiting && (g.viewportFocused || g.selectedId) && !(lp & (1 << 30))) {
            if (wp == 'Q') { g.gizmoMode = 1; return 0; }
            if (wp == 'W') { g.gizmoMode = 0; return 0; }
            if (wp == 'E') { g.gizmoMode = 2; return 0; }
        }
        // blueprint key state + fresh-press events
        for (int i = 0; i < BP_NKEYS; i++) {
            if ((int)wp == BP_KEY_VKS[i]) {
                g.bpKeysDown[i] = true;
                if (g.mode == Mode::Play && !(lp & (1 << 30))) g.bpKeyEvents.push_back(i);
            }
        }
        bool onBP = bp != nullptr;   // a blueprint tab is open fullscreen
        bool onAnimation = g.mode == Mode::Edit && g.activeDoc == 0 && g.animationFocused;
        // the widget Designer owns its own component clipboard (copy / paste / duplicate)
        bool onWidget = activeWidget() && !activeWidget()->graphMode;
        if (GetKeyState(VK_CONTROL) & 0x8000) {
            if (wp == 'C') { if (onBP || onAnimation || onWidget) g.uiIn.keyCopy = true; else copySelection(); }
            else if (wp == 'V') { if (onBP || onAnimation || onWidget) g.uiIn.keyPaste = true; else pasteClipboard(); }
            else if (wp == 'A' && onAnimation) g.uiIn.keySelectAll = true;
            else if (wp == 'D' && onWidget) g.uiIn.keyDuplicate = true;
            else if (wp == 'D' && !onBP && !onAnimation) {
                Entity* sel = g.scene.byId(g.selectedId);
                if (sel) duplicateSceneEntity(sel->id);
            }
            return 0;
        }
        // Delete over the Content browser/drawer removes the selected assets. The
        // drawContenutiContent pass (dock panel or drawer) consumes this and asks
        // for confirmation, operating on whichever browser view is under the cursor.
        if (wp == VK_DELETE && !(lp & (1 << 30)) && (drawerCoversPointerThisFrame() ||
            (g.activeDoc == 0 && g.dock.windowHovered("contenuti", g.uiIn.mouseX, g.uiIn.mouseY)))) {
            g.browserDeletePending = true; return 0;
        }
        // Del also reaches the document editors that own a selection of their own
        // (widget designer, curve, material), not just blueprints/animation.
        if (wp == VK_DELETE && (onBP || onAnimation || activeWidget() || activeMaterial() || activeCurve())) {
            g.uiIn.keyDelete = true; return 0;
        }
        if (wp == VK_ESCAPE) { g.uiIn.keyEscape = true; if (!onBP) g.selectedId = 0; return 0; }
        if (onBP) {
            // Canvas shortcuts such as C (create Comment) need the raw key.
            if (!(lp & (1 << 30))) g.uiIn.keyPressedVK = (int)wp;
            return 0;
        }
        switch (wp) {
        case VK_SPACE:
            if (g.mode == Mode::Play) g.spaceQueued = true;
            else play();
            break;
        case 'B': shootBall(); break;
        case 'F': {
            Entity* sel = g.scene.byId(g.selectedId);
            if (sel) {
                g.camera.target = sel->body->position;
                float r = sel->scale.length();
                g.camera.distance = r * 3.5f < 3 ? 3 : r * 3.5f;
            }
            break;
        }
        case VK_DELETE: {
            Entity* sel = g.scene.byId(g.selectedId);
            if (sel && g.mode == Mode::Edit && (g.viewportFocused || g.outlinerFocused)) {
                addLog(0, "Deleted: %s", sel->name);
                std::vector<int> audioIds; g.scene.collectSubtree(sel->id, audioIds);
                for (int audioId : audioIds) g.audio.stop(audioId);
                g.scene.removeEntity(sel->id);
                g.selectedId = 0;
                g.selectedIds.clear();
            } else if (!g.viewportFocused && !g.outlinerFocused) {
                // Delete belongs to the focused editor. In every other panel
                // it clears the stale Outliner selection instead of deleting.
                g.selectedId = 0;
                g.selectedIds.clear();
            }
            break;
        }
        }
        return 0;
    }
    }
    return DefWindowProcA(hwnd, msg, wp, lp);
}

// ═══ detached native windows (one GL context shared across every window) ═══
static LRESULT CALLBACK PanelWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    NativeWin* nw = (NativeWin*)GetWindowLongPtrA(hwnd, GWLP_USERDATA);
    if (!nw) return DefWindowProcA(hwnd, msg, wp, lp);
    switch (msg) {
    case WM_CLOSE:
        nw->wantClose = true;   // handled by the frame loop: panel returns in-app
        return 0;
    case WM_SIZE:
        nw->width = LOWORD(lp) > 1 ? LOWORD(lp) : 1;
        nw->height = HIWORD(lp) > 1 ? HIWORD(lp) : 1;
        return 0;
    case WM_PAINT:
        ValidateRect(hwnd, nullptr);
        return 0;
    case WM_LBUTTONDOWN:
        nw->in.mouseX = (float)GET_X_LPARAM(lp);
        nw->in.mouseY = (float)GET_Y_LPARAM(lp);
        nw->in.mousePressed = true;
        nw->in.mouseDown = true;
        SetCapture(hwnd);
        return 0;
    case WM_LBUTTONUP:
        nw->in.mouseReleased = true;
        nw->in.mouseDown = false;
        ReleaseCapture();
        return 0;
    case WM_RBUTTONDOWN:
        nw->in.rmbPressed = true;
        nw->in.rmbDown = true;
        // popped-out viewport: same navigation as the docked one (RMB orbit + WASD)
        if (nw->dockId == "viewport") {
            nw->in.mouseX = (float)GET_X_LPARAM(lp);
            nw->in.mouseY = (float)GET_Y_LPARAM(lp);
            g.orbiting = true;
            SetCapture(hwnd);
        }
        return 0;
    case WM_RBUTTONUP:
        nw->in.rmbReleased = true;
        nw->in.rmbDown = false;
        if (nw->dockId == "viewport") { g.orbiting = false; if (!g.panning) ReleaseCapture(); }
        return 0;
    case WM_MBUTTONDOWN:
        if (nw->dockId == "viewport") {
            nw->in.mouseX = (float)GET_X_LPARAM(lp);
            nw->in.mouseY = (float)GET_Y_LPARAM(lp);
            g.panning = true;
            SetCapture(hwnd);
        }
        return 0;
    case WM_MBUTTONUP:
        if (nw->dockId == "viewport") { g.panning = false; if (!g.orbiting) ReleaseCapture(); }
        return 0;
    case WM_MOUSEMOVE: {
        int x = GET_X_LPARAM(lp), y = GET_Y_LPARAM(lp);
        if (nw->dockId == "viewport") {
            int dx = x - (int)nw->in.mouseX, dy = y - (int)nw->in.mouseY;
            if (g.orbiting) g.camera.freeLook((float)dx, (float)dy);
            else if (g.panning) g.camera.pan((float)dx, (float)dy);
        }
        nw->in.mouseX = (float)x;
        nw->in.mouseY = (float)y;
        return 0;
    }
    case WM_MOUSEWHEEL: {
        float notches = (float)GET_WHEEL_DELTA_WPARAM(wp) / WHEEL_DELTA;
        if (nw->dockId == "viewport") g.camera.zoom(-notches * 120);
        else nw->in.wheel += notches;
        return 0;
    }
    case WM_CHAR:
        if (nw->in.typedCount < 31) {
            char c = (char)wp;
            if (c >= 32 && c < 127) nw->in.typed[nw->in.typedCount++] = c;
        }
        return 0;
    case WM_KEYDOWN:
        if (gBPEditor.listeningKey()) {
            if (wp == VK_ESCAPE) nw->in.keyEscape = true;
            else nw->in.keyPressedVK = (int)wp;
            return 0;
        }
        if (wp == VK_BACK) nw->in.keyBackspace = true;
        if (wp == VK_RETURN) nw->in.keyEnter = true;
        if (wp == VK_ESCAPE) nw->in.keyEscape = true;
        if (wp == VK_DELETE) nw->in.keyDelete = true;
        if (wp == VK_LEFT) nw->in.keyLeft = true;
        if (wp == VK_RIGHT) nw->in.keyRight = true;
        if (GetKeyState(VK_CONTROL) & 0x8000) {
            if (wp == 'A') nw->in.keySelectAll = true;
            if (wp == 'C') nw->in.keyCopy = true;
            else if (wp == 'V') nw->in.keyPaste = true;
            else if (wp == 'D') nw->in.keyDuplicate = true;
        }
        return 0;
    }
    return DefWindowProcA(hwnd, msg, wp, lp);
}

static void createNativeWindow(const std::string& dockId, const std::string& title, int scrX, int scrY, int w, int h) {
    static bool classDone = false;
    HINSTANCE hInst = GetModuleHandleA(nullptr);
    if (!classDone) {
        WNDCLASSA cls = {};
        cls.style = CS_OWNDC;
        cls.lpfnWndProc = PanelWndProc;
        cls.hInstance = hInst;
        cls.hCursor = LoadCursorA(nullptr, (LPCSTR)IDC_ARROW);
        cls.lpszClassName = "PulsePanel";
        RegisterClassA(&cls);
        classDone = true;
    }
    auto nw = std::make_unique<NativeWin>();
    nw->dockId = dockId;
    nw->width = w > 240 ? w : 560;
    nw->height = h > 160 ? h : 380;
    RECT rc = { 0, 0, nw->width, nw->height };
    AdjustWindowRect(&rc, WS_OVERLAPPEDWINDOW, FALSE);
    char titolo[128];
    snprintf(titolo, sizeof(titolo), "Pulse Engine - %s", title.c_str());
    nw->hwnd = CreateWindowA("PulsePanel", titolo, WS_OVERLAPPEDWINDOW,
                             scrX, scrY, rc.right - rc.left, rc.bottom - rc.top,
                             nullptr, nullptr, hInst, nullptr);
    if (!nw->hwnd) return;
    SetWindowLongPtrA(nw->hwnd, GWLP_USERDATA, (LONG_PTR)nw.get());
    nw->hdc = GetDC(nw->hwnd);
    SetPixelFormat(nw->hdc, g.pixelFormat, &g.pfd);
    ShowWindow(nw->hwnd, SW_SHOW);
    addLog(1, "Panel '%s' popped out into its own window (close it to bring it back).", title.c_str());
    g.natives.push_back(std::move(nw));
}

static void renderNativeWindows() {
    if (g.natives.empty()) return;
    for (auto& up : g.natives) {
        NativeWin& nw = *up;
        wglMakeCurrent(nw.hdc, g.glrc);
        if (wglSwapIntervalEXT) wglSwapIntervalEXT(0);   // only the main window waits vsync
        g.renderer.resize(nw.width, nw.height);
        glViewport(0, 0, nw.width, nw.height);
        glDisable(GL_SCISSOR_TEST);
        glClearColor(0.09f, 0.10f, 0.115f, 1);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        nw.in.keyCtrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
        nw.in.keyAlt = (GetKeyState(VK_MENU) & 0x8000) != 0;
        nw.in.keyShift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
        if (nw.dockId == "viewport") {
            // the viewport's content is the 3D scene, not a UI panel: render it into
            // the whole popped-out window (mirrors the main camera, aspect fitted)
            if (g.frameForRender && nw.width > 0 && nw.height > 0) {
                OrbitCamera cam = g.camera;
                cam.update((float)nw.width / (float)nw.height);
                g.renderer.render(*g.frameForRender, cam, 0, 0, nw.width, nw.height);
            }
        } else {
            nw.ui.begin(&g.renderer, nw.in);
            for (const auto& icon : g.assetIconTextures) nw.ui.setAssetIcon(icon.first, icon.second);
            char pid[80];
            snprintf(pid, sizeof(pid), "nat_%s", nw.dockId.c_str());
            nw.ui.panelBegin(pid, 0, 0, (float)nw.width, (float)nw.height, nullptr);
            windowContent(nw.ui, nw.dockId);
            nw.ui.panelEnd();
            nw.ui.end();
        }
        SwapBuffers(nw.hdc);
        // per-frame input reset (events accumulate during the next message pump)
        nw.in.mousePressed = nw.in.mouseReleased = false;
        nw.in.rmbPressed = nw.in.rmbReleased = false;
        nw.in.wheel = 0;
        nw.in.typedCount = 0;
        nw.in.keyBackspace = nw.in.keyEnter = nw.in.keyEscape = nw.in.keyDelete = false;
        nw.in.keyLeft = nw.in.keyRight = nw.in.keySelectAll = false;
        nw.in.keyCopy = nw.in.keyPaste = nw.in.keyDuplicate = false;
        nw.in.keyPressedVK = 0;
    }
    wglMakeCurrent(g.hdc, g.glrc);
    if (wglSwapIntervalEXT) wglSwapIntervalEXT(g.vsync ? 1 : 0);
    g.renderer.resize(g.width, g.height);
    glViewport(0, 0, g.width, g.height);
}

static void handleNativeWindows() {
    // pop-out requests from the dock (button, or tab dropped outside the window)
    if (!g.dock.popOutRequest.empty()) {
        DockWindow* w = g.dock.find(g.dock.popOutRequest.c_str());
        g.dock.popOutRequest.clear();
        if (w) {
            bool exists = false;
            for (auto& up : g.natives) if (up->dockId == w->id) exists = true;
            if (!exists) {
                POINT p = { (LONG)g.dock.popOutX, (LONG)g.dock.popOutY };
                ClientToScreen(g.hwnd, &p);
                w->area = DOCK_NATIVE;
                createNativeWindow(w->id, w->title, p.x, p.y, (int)w->rect.w, (int)w->rect.h);
            }
        }
    }
    // closed native windows come back as in-app floating panels
    for (auto it = g.natives.begin(); it != g.natives.end();) {
        if ((*it)->wantClose) {
            DockWindow* w = g.dock.find((*it)->dockId.c_str());
            if (w) {
                w->area = DOCK_FLOAT;
                w->open = true;
                w->rect = { 320, 90, 620, 420 };
            }
            SetWindowLongPtrA((*it)->hwnd, GWLP_USERDATA, 0);
            ReleaseDC((*it)->hwnd, (*it)->hdc);
            DestroyWindow((*it)->hwnd);
            it = g.natives.erase(it);
        } else {
            ++it;
        }
    }
}

static bool createGLWindow(HINSTANCE hInst, bool visible) {
    WNDCLASSA dummyCls = {};
    dummyCls.style = CS_OWNDC;
    dummyCls.lpfnWndProc = DefWindowProcA;
    dummyCls.hInstance = hInst;
    dummyCls.lpszClassName = "PulseDummy";
    RegisterClassA(&dummyCls);
    HWND dummyWnd = CreateWindowA("PulseDummy", "", WS_OVERLAPPEDWINDOW, 0, 0, 32, 32, nullptr, nullptr, hInst, nullptr);
    HDC dummyDC = GetDC(dummyWnd);
    PIXELFORMATDESCRIPTOR pfd = {};
    pfd.nSize = sizeof(pfd);
    pfd.nVersion = 1;
    pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
    pfd.iPixelType = PFD_TYPE_RGBA;
    pfd.cColorBits = 32;
    pfd.cDepthBits = 24;
    SetPixelFormat(dummyDC, ChoosePixelFormat(dummyDC, &pfd), &pfd);
    HGLRC dummyRC = wglCreateContext(dummyDC);
    wglMakeCurrent(dummyDC, dummyRC);
    loadWGLExtensions();
    wglMakeCurrent(nullptr, nullptr);
    wglDeleteContext(dummyRC);
    ReleaseDC(dummyWnd, dummyDC);
    DestroyWindow(dummyWnd);

    WNDCLASSA cls = {};
    cls.style = CS_OWNDC | CS_HREDRAW | CS_VREDRAW;
    cls.lpfnWndProc = WndProc;
    cls.hInstance = hInst;
    cls.hCursor = LoadCursorA(nullptr, (LPCSTR)IDC_ARROW);
    cls.lpszClassName = "PulseEngine";
    RegisterClassA(&cls);

    RECT r = { 0, 0, g.width, g.height };
    AdjustWindowRect(&r, WS_OVERLAPPEDWINDOW, FALSE);
    int sw = GetSystemMetrics(SM_CXSCREEN), sh = GetSystemMetrics(SM_CYSCREEN);
    int ww = r.right - r.left, wh = r.bottom - r.top;
    g.hwnd = CreateWindowA("PulseEngine", "Pulse Engine - Editor",
                           WS_OVERLAPPEDWINDOW, (sw - ww) / 2, (sh - wh) / 2, ww, wh,
                           nullptr, nullptr, hInst, nullptr);
    if (!g.hwnd) return false;
    g.hdc = GetDC(g.hwnd);

    int pf = 0;
    UINT numFormats = 0;
    if (wglChoosePixelFormatARB) {
        const int attribs[] = {
            WGL_DRAW_TO_WINDOW_ARB, 1,
            WGL_SUPPORT_OPENGL_ARB, 1,
            WGL_DOUBLE_BUFFER_ARB, 1,
            WGL_ACCELERATION_ARB, WGL_FULL_ACCELERATION_ARB,
            WGL_PIXEL_TYPE_ARB, WGL_TYPE_RGBA_ARB,
            WGL_COLOR_BITS_ARB, 32,
            WGL_DEPTH_BITS_ARB, 24,
            WGL_SAMPLE_BUFFERS_ARB, 1,
            WGL_SAMPLES_ARB, 4,
            0,
        };
        wglChoosePixelFormatARB(g.hdc, attribs, nullptr, 1, &pf, &numFormats);
        if (!numFormats) {
            const int attribsNoMsaa[] = {
                WGL_DRAW_TO_WINDOW_ARB, 1,
                WGL_SUPPORT_OPENGL_ARB, 1,
                WGL_DOUBLE_BUFFER_ARB, 1,
                WGL_PIXEL_TYPE_ARB, WGL_TYPE_RGBA_ARB,
                WGL_COLOR_BITS_ARB, 32,
                WGL_DEPTH_BITS_ARB, 24,
                0,
            };
            wglChoosePixelFormatARB(g.hdc, attribsNoMsaa, nullptr, 1, &pf, &numFormats);
        }
    }
    if (!pf) pf = ChoosePixelFormat(g.hdc, &pfd);
    DescribePixelFormat(g.hdc, pf, sizeof(pfd), &pfd);
    SetPixelFormat(g.hdc, pf, &pfd);
    g.pixelFormat = pf;
    g.pfd = pfd;

    HGLRC rc = nullptr;
    if (wglCreateContextAttribsARB) {
        const int ctxAttribs[] = {
            WGL_CONTEXT_MAJOR_VERSION_ARB, 3,
            WGL_CONTEXT_MINOR_VERSION_ARB, 3,
            WGL_CONTEXT_PROFILE_MASK_ARB, WGL_CONTEXT_CORE_PROFILE_BIT_ARB,
            0,
        };
        rc = wglCreateContextAttribsARB(g.hdc, nullptr, ctxAttribs);
    }
    if (!rc) rc = wglCreateContext(g.hdc);
    if (!rc) return false;
    g.glrc = rc;
    wglMakeCurrent(g.hdc, rc);
    if (!loadGLFunctions()) {
        MessageBoxA(nullptr, "Could not load the OpenGL 3.3 functions", "Pulse Engine", MB_ICONERROR);
        return false;
    }
    if (wglSwapIntervalEXT) wglSwapIntervalEXT(1);

    if (visible) {
        ShowWindow(g.hwnd, SW_SHOW);
        UpdateWindow(g.hwnd);
    }
    return true;
}

// ═══ BMP screenshot ═══
static bool saveBMP(const char* path, int w, int h) {
    std::vector<unsigned char> rgba(w * h * 4);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());
    int rowSize = ((w * 3 + 3) / 4) * 4;
    BITMAPFILEHEADER bfh = {};
    BITMAPINFOHEADER bih = {};
    bfh.bfType = 0x4D42;
    bfh.bfOffBits = sizeof(bfh) + sizeof(bih);
    bfh.bfSize = bfh.bfOffBits + rowSize * h;
    bih.biSize = sizeof(bih);
    bih.biWidth = w;
    bih.biHeight = h;
    bih.biPlanes = 1;
    bih.biBitCount = 24;
    bih.biCompression = BI_RGB;
    FILE* f = fopen(path, "wb");
    if (!f) return false;
    fwrite(&bfh, sizeof(bfh), 1, f);
    fwrite(&bih, sizeof(bih), 1, f);
    std::vector<unsigned char> row(rowSize, 0);
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            row[x * 3 + 0] = rgba[(y * w + x) * 4 + 2];
            row[x * 3 + 1] = rgba[(y * w + x) * 4 + 1];
            row[x * 3 + 2] = rgba[(y * w + x) * 4 + 0];
        }
        fwrite(row.data(), rowSize, 1, f);
    }
    fclose(f);
    return true;
}

// ═══ headless tests ═══
static int runTests() {
    FILE* out = fopen("test_results.txt", "w");
    auto report = [&](const char* name, bool pass, const char* detail) {
        char line[256];
        snprintf(line, sizeof(line), "[%s] %s - %s\n", pass ? "PASS" : "FAIL", name, detail);
        printf("%s", line);
        if (out) fputs(line, out);
    };
    int failures = 0;
    char detail[128];
    EditorScene& s = g.scene;

    {
        s.clear();
        s.spawnBox("terra", { 0, -0.5f, 0 }, { 26, 1, 26 }, {}, BodyType::Static, 0, 0.25f, 0.7f);
        Entity& c = s.spawnBox("cubo", { 0, 2, 0 }, { 1, 1, 1 }, {}, BodyType::Dynamic, 1);
        for (int i = 0; i < 180; i++) s.world.step(FIXED_DT);
        bool pass = fabsf(c.body->position.y - 0.5f) < 0.03f && c.body->sleeping;
        snprintf(detail, sizeof(detail), "cubo fermo a y=%.4f, sleeping=%d", c.body->position.y, c.body->sleeping);
        report("Riposo e sleeping", pass, detail);
        if (!pass) failures++;
    }
    {
        s.clear();
        s.spawnBox("terra", { 0, -0.5f, 0 }, { 20, 1, 20 }, {}, BodyType::Static, 0, 0.1f, 0.8f);
        int capsuleId = s.spawnBox("capsula", { -3, 3, 0 }, { 1, 1, 1 }, {}, BodyType::Dynamic, 1).id;
        int cylinderId = s.spawnBox("cilindro", { 0, 3, 0 }, { 1, 1, 1 }, {}, BodyType::Dynamic, 1).id;
        int coneId = s.spawnBox("cono", { 3, 3, 0 }, { 1, 1, 1 }, {}, BodyType::Dynamic, 1).id;
        Entity& capsule = *s.byId(capsuleId); capsule.mesh = MESH_CAPSULE; capsule.scale = { 1, 1, 1 }; s.syncBodyShape(capsule);
        Entity& cylinder = *s.byId(cylinderId); cylinder.mesh = MESH_CYLINDER; cylinder.scale = { 1, 1, 1 }; s.syncBodyShape(cylinder);
        Entity& cone = *s.byId(coneId); cone.mesh = MESH_CONE; cone.scale = { 1, 1, 1 }; s.syncBodyShape(cone);
        RigidBody* ground = s.world.bodies.front().get();
        Contact probe[4];
        capsule.body->position.y = 0.9f; int capsuleProbe = collide(*ground, *capsule.body, probe); capsule.body->position.y = 3;
        cylinder.body->position.y = 0.4f; int cylinderProbe = collide(*ground, *cylinder.body, probe); cylinder.body->position.y = 3;
        cone.body->position.y = 0.4f; int coneProbe = collide(*ground, *cone.body, probe); cone.body->position.y = 3;
        for (int i = 0; i < 360; i++) s.world.step(FIXED_DT);
        bool kinds = capsule.body->shape.kind == ShapeKind::Capsule && cylinder.body->shape.kind == ShapeKind::Cylinder &&
                     cone.body->shape.kind == ShapeKind::Cone;
        bool stable = capsule.body->position.y > -0.2f && cylinder.body->position.y > -0.2f && cone.body->position.y > -0.2f &&
                      capsule.body->position.y < 3 && cylinder.body->position.y < 3 && cone.body->position.y < 3;
        bool pass = kinds && stable;
        snprintf(detail, sizeof(detail), "probe=%d/%d/%d; y=%.1f/%.1f/%.1f", capsuleProbe, cylinderProbe, coneProbe,
                 capsule.body->position.y, cylinder.body->position.y, cone.body->position.y);
        report("Collider primitivi dedicati", pass, detail);
        if (!pass) failures++;
    }
    {
        s.clear();
        s.spawnBox("terra", { 0, -0.5f, 0 }, { 20, 1, 20 }, {}, BodyType::Static, 0, 0.05f, 0.8f);
        int capsuleId = s.spawnBox("capsula inclinata", { 0, 3, 0 }, { 1, 1, 1 }, {}, BodyType::Dynamic, 1,
                                   0.05f, 0.8f, Quat::fromEulerDeg(43, 0, 0)).id;
        Entity& capsule = *s.byId(capsuleId);
        capsule.mesh = MESH_CAPSULE; capsule.scale = { 1, 1, 1 }; s.syncBodyShape(capsule);
        Vec3 initialAxis = capsule.body->quat.rotate({ 0, 1, 0 });
        for (int i = 0; i < 420; i++) s.world.step(FIXED_DT);
        Vec3 finalAxis = capsule.body->quat.rotate({ 0, 1, 0 });
        float lowest = capsule.body->position.y - fabsf(finalAxis.y) * capsule.body->shape.halfHeight - capsule.body->shape.radius;
        float axisChange = 1.0f - fabsf(initialAxis.dot(finalAxis));
        bool pass = lowest > -0.04f && lowest < 0.08f && capsule.body->position.y > 0.45f && axisChange > 0.05f;
        snprintf(detail, sizeof(detail), "fondo=%.3f, rotazione collisione=%.3f, y=%.3f", lowest, axisChange,
                 capsule.body->position.y);
        report("Capsula inclinata: contatto e coppia", pass, detail);
        if (!pass) failures++;
    }
    {
        s.clear();
        int capsuleId = s.spawnBox("capsula dinamica", { -2.5f, 0, 0 }, { 1, 1, 1 }, {}, BodyType::Dynamic, 1,
                                   0.2f, 0.2f, Quat::fromEulerDeg(90, 0, 0)).id;
        int sphereId = s.spawnSphere("sfera dinamica", { 1, 0, 0 }, 1, {}, 1, 0.2f, 0.2f).id;
        Entity& capsule = *s.byId(capsuleId); capsule.mesh = MESH_CAPSULE; capsule.scale = { 1, 1, 1 }; s.syncBodyShape(capsule);
        Entity& sphere = *s.byId(sphereId);
        capsule.body->useGravity = false; sphere.body->useGravity = false;
        capsule.body->canSleep = false; sphere.body->canSleep = false;
        capsule.body->velocity = { 5, 0, 0 };
        for (int i = 0; i < 90; i++) s.world.step(FIXED_DT);
        bool pass = sphere.body->velocity.x > 0.5f && sphere.body->position.x > 1.2f && capsule.body->position.x < sphere.body->position.x;
        snprintf(detail, sizeof(detail), "vCaps=%.2f, vSfera=%.2f, x=%.2f/%.2f", capsule.body->velocity.x,
                 sphere.body->velocity.x, capsule.body->position.x, sphere.body->position.x);
        report("RigidBody dinamici: capsula-sfera", pass, detail);
        if (!pass) failures++;
    }
    {
        s.clear();
        int aId = s.spawnBox("capsula A", { -2, 0, 0 }, { 1, 1, 1 }, {}, BodyType::Dynamic, 1, 0.25f, 0.2f).id;
        int bId = s.spawnBox("capsula B", { 2, 0, 0 }, { 1, 1, 1 }, {}, BodyType::Dynamic, 1, 0.25f, 0.2f).id;
        Entity& a = *s.byId(aId); a.mesh = MESH_CAPSULE; a.scale = { 1, 1, 1 }; s.syncBodyShape(a);
        Entity& b = *s.byId(bId); b.mesh = MESH_CAPSULE; b.scale = { 1, 1, 1 }; s.syncBodyShape(b);
        a.body->useGravity = false; b.body->useGravity = false;
        a.body->canSleep = false; b.body->canSleep = false;
        a.body->velocity = { 3, 0, 0 }; b.body->velocity = { -3, 0, 0 };
        for (int i = 0; i < 90; i++) s.world.step(FIXED_DT);
        bool pass = a.body->position.x < b.body->position.x && a.body->velocity.x < -0.2f && b.body->velocity.x > 0.2f;
        snprintf(detail, sizeof(detail), "v=%.2f/%.2f, x=%.2f/%.2f", a.body->velocity.x, b.body->velocity.x,
                 a.body->position.x, b.body->position.x);
        report("RigidBody dinamici: capsula-capsula", pass, detail);
        if (!pass) failures++;
    }
    {
        s.clear();
        s.spawnBox("terra", { 0, -0.5f, 0 }, { 30, 1, 30 }, {}, BodyType::Static, 0, 0, 0.95f);
        Entity& sphere = s.spawnSphere("sfera rotolante", { 0, 0.52f, 0 }, 1, {}, 1, 0, 0.95f);
        sphere.body->canSleep = false;
        sphere.body->velocity = { 0, 0, 4 };
        for (int i = 0; i < 120; i++) s.world.step(FIXED_DT);
        float spin = fabsf(sphere.body->angularVelocity.x);
        bool pass = spin > 0.5f && sphere.body->position.y > 0.42f && sphere.body->position.y < 0.6f;
        snprintf(detail, sizeof(detail), "spin=%.3f, velocita=%.3f, y=%.3f", spin,
                 sphere.body->velocity.z, sphere.body->position.y);
        report("Rotolamento sfera", pass, detail);
        if (!pass) failures++;
    }
    {
        s.clear();
        s.spawnBox("terra", { 0, -0.5f, 0 }, { 30, 1, 30 }, {}, BodyType::Static, 0, 0, 0.95f);
        int id = s.spawnBox("cilindro sdraiato", { 0, 0.52f, 0 }, { 1, 2, 1 }, {}, BodyType::Dynamic, 1,
                            0, 0.95f, Quat::fromEulerDeg(90, 0, 0)).id;
        Entity& cylinder = *s.byId(id);
        cylinder.mesh = MESH_CYLINDER; cylinder.scale = { 1, 2, 1 }; s.syncBodyShape(cylinder);
        cylinder.body->canSleep = false;
        cylinder.body->velocity = { 0, 0, 4 };
        Vec3 cylinderAxis = cylinder.body->quat.rotate({ 0, 1, 0 });
        for (int i = 0; i < 120; i++) s.world.step(FIXED_DT);
        float spin = fabsf(cylinder.body->angularVelocity.dot(cylinderAxis));
        bool pass = spin > 0.5f && cylinder.body->position.y > 0.42f && cylinder.body->position.y < 0.7f;
        snprintf(detail, sizeof(detail), "spin asse=%.3f, velocita=%.3f, y=%.3f", spin,
                 cylinder.body->velocity.z, cylinder.body->position.y);
        report("Rotolamento cilindro sdraiato", pass, detail);
        if (!pass) failures++;
    }
    {
        s.clear();
        s.spawnBox("terra", { 0, -0.5f, 0 }, { 30, 1, 30 }, {}, BodyType::Static, 0, 0, 0.9f);
        int id = s.spawnBox("cilindro inclinato", { 0, 3, 0 }, { 1, 2, 1 }, {}, BodyType::Dynamic, 1,
                            0, 0.8f, Quat::fromEulerDeg(38, 0, 0)).id;
        Entity& cylinder = *s.byId(id);
        cylinder.mesh = MESH_CYLINDER; cylinder.scale = { 1, 2, 1 }; s.syncBodyShape(cylinder);
        Vec3 initialAxis = cylinder.body->quat.rotate({ 0, 1, 0 });
        for (int i = 0; i < 420; i++) s.world.step(FIXED_DT);
        Vec3 finalAxis = cylinder.body->quat.rotate({ 0, 1, 0 });
        float radialY = sqrtf(std::max(0.0f, 1.0f - finalAxis.y * finalAxis.y));
        float lowest = cylinder.body->position.y - fabsf(finalAxis.y) * cylinder.body->shape.halfHeight
                     - radialY * cylinder.body->shape.radius;
        float axisChange = 1.0f - fabsf(initialAxis.dot(finalAxis));
        bool pass = lowest > -0.06f && lowest < 0.1f && axisChange > 0.05f;
        snprintf(detail, sizeof(detail), "fondo=%.3f, rotazione urto=%.3f, y=%.3f", lowest, axisChange,
                 cylinder.body->position.y);
        report("Cilindro inclinato: contatto sul bordo", pass, detail);
        if (!pass) failures++;
    }
    {
        s.clear();
        s.spawnBox("terra", { 0, -0.5f, 0 }, { 30, 1, 30 }, {}, BodyType::Static, 0, 0, 0.9f);
        int id = s.spawnBox("cono inclinato", { 0, 3, 0 }, { 1, 2, 1 }, {}, BodyType::Dynamic, 1,
                            0, 0.8f, Quat::fromEulerDeg(32, 0, 0)).id;
        Entity& cone = *s.byId(id);
        cone.mesh = MESH_CONE; cone.scale = { 1, 2, 1 }; s.syncBodyShape(cone);
        Vec3 initialAxis = cone.body->quat.rotate({ 0, 1, 0 });
        for (int i = 0; i < 480; i++) s.world.step(FIXED_DT);
        Vec3 finalAxis = cone.body->quat.rotate({ 0, 1, 0 });
        float lowest = (cone.body->position + cone.body->quat.rotate({ 0, cone.body->shape.halfHeight, 0 })).y;
        for (int i = 0; i < 48; i++) {
            float t = 6.28318530718f * i / 48.0f;
            Vec3 p = { cosf(t) * cone.body->shape.radius, -cone.body->shape.halfHeight,
                       sinf(t) * cone.body->shape.radius };
            lowest = std::min(lowest, (cone.body->position + cone.body->quat.rotate(p)).y);
        }
        float axisChange = 1.0f - fabsf(initialAxis.dot(finalAxis));
        bool pass = lowest > -0.08f && lowest < 0.12f && axisChange > 0.03f;
        snprintf(detail, sizeof(detail), "fondo=%.3f, rotazione urto=%.3f, y=%.3f", lowest, axisChange,
                 cone.body->position.y);
        report("Cono inclinato: contatto sul bordo", pass, detail);
        if (!pass) failures++;
    }
    {
        s.clear();
        s.spawnBox("terra", { 0, -0.5f, 0 }, { 26, 1, 26 }, {}, BodyType::Static, 0, 0.9f, 0.3f);
        Entity& sp = s.spawnSphere("palla", { 0, 3, 0 }, 1, {}, 1, 0.9f, 0.3f);
        float maxAfter = 0;
        bool bounced = false;
        for (int i = 0; i < 240; i++) {
            s.world.step(FIXED_DT);
            if (sp.body->velocity.y > 0.5f) bounced = true;
            if (bounced && sp.body->position.y > maxAfter) maxAfter = sp.body->position.y;
        }
        bool pass = bounced && maxAfter > 1.4f && maxAfter < 2.9f;
        snprintf(detail, sizeof(detail), "apice dopo rimbalzo y=%.2f", maxAfter);
        report("Restitution", pass, detail);
        if (!pass) failures++;
    }
    {
        // layer di collisione: layer 0 e 3 disattivati → il cubo attraversa il pavimento
        s.clear();
        int terraId = s.spawnBox("terra", { 0, -0.5f, 0 }, { 26, 1, 26 }, {}, BodyType::Static, 0).id;
        s.byId(terraId)->layer = 3;
        s.byId(terraId)->body->layer = 3;   // terra sul layer 3
        int cuboId = s.spawnBox("cubo", { 0, 3, 0 }, { 1, 1, 1 }, {}, BodyType::Dynamic, 1).id;  // layer 0
        s.layers.matrix[0][3] = s.layers.matrix[3][0] = false;
        s.applyLayersToWorld();
        for (int i = 0; i < 120; i++) s.world.step(FIXED_DT);
        float yThrough = s.byId(cuboId)->body->position.y;
        // riattiva la collisione → il cubo si ferma sul pavimento
        s.clear();
        terraId = s.spawnBox("terra", { 0, -0.5f, 0 }, { 26, 1, 26 }, {}, BodyType::Static, 0).id;
        s.byId(terraId)->layer = 3;
        s.byId(terraId)->body->layer = 3;
        cuboId = s.spawnBox("cubo", { 0, 3, 0 }, { 1, 1, 1 }, {}, BodyType::Dynamic, 1).id;
        s.applyLayersToWorld();   // matrice di default: tutti collidono
        for (int i = 0; i < 180; i++) s.world.step(FIXED_DT);
        float yBlock = s.byId(cuboId)->body->position.y;
        bool pass = yThrough < -1.0f && fabsf(yBlock - 0.5f) < 0.05f;
        snprintf(detail, sizeof(detail), "attraversa y=%.2f (<-1), blocca y=%.2f (0.5)", yThrough, yBlock);
        report("Collision layers", pass, detail);
        if (!pass) failures++;
    }
    {
        // Blueprint traces must use the selected bit mask, independently from
        // the physics response matrix.
        s.clear();
        int ownerId=s.spawnEmpty("trace_owner",{}).id;
        int targetId=s.spawnBox("trace_target",{5,0,0},{1,1,1},{},BodyType::Static,0).id;
        s.byId(targetId)->layer=3;s.byId(targetId)->body->layer=3;
        BPGraph graph;
        BPVarDef miss;snprintf(miss.name,sizeof(miss.name),"Miss");miss.type=PIN_BOOL;graph.vars.push_back(miss);
        BPVarDef hit;snprintf(hit.name,sizeof(hit.name),"Hit");hit.type=PIN_BOOL;graph.vars.push_back(hit);
        BPVarDef actor;snprintf(actor.name,sizeof(actor.name),"Actor");actor.type=PIN_ENT;graph.vars.push_back(actor);
        BPCanvas& canvas=graph.main();int start=canvas.addNode(BP_EV_START,0,0);
        int missTrace=canvas.addNode(BP_TRACE_LINE,180,0);canvas.byId(missTrace)->choice=1<<2;
        canvas.byId(missTrace)->lit[1]={0,0,0};canvas.byId(missTrace)->lit[2]={10,0,0};
        int setMiss=canvas.addNode(BP_VAR_SET,380,0);snprintf(canvas.byId(setMiss)->sname,sizeof(canvas.byId(setMiss)->sname),"Miss");
        int hitTrace=canvas.addNode(BP_TRACE_LINE,560,0);canvas.byId(hitTrace)->choice=1<<3;
        canvas.byId(hitTrace)->lit[1]={0,0,0};canvas.byId(hitTrace)->lit[2]={10,0,0};
        int setHit=canvas.addNode(BP_VAR_SET,760,0);snprintf(canvas.byId(setHit)->sname,sizeof(canvas.byId(setHit)->sname),"Hit");
        int setActor=canvas.addNode(BP_VAR_SET,940,0);snprintf(canvas.byId(setActor)->sname,sizeof(canvas.byId(setActor)->sname),"Actor");
        canvas.connect(start,0,missTrace,0);canvas.connect(missTrace,0,setMiss,0);canvas.connect(missTrace,1,setMiss,1);
        canvas.connect(setMiss,0,hitTrace,0);canvas.connect(hitTrace,0,setHit,0);canvas.connect(hitTrace,1,setHit,1);
        canvas.connect(setHit,0,setActor,0);canvas.connect(hitTrace,4,setActor,1);
        BPInstance instance;instance.graph=&graph;instance.entity=s.byId(ownerId);instance.initVars(nullptr);
        BPContext context;context.entity=instance.entity;context.scene=&s;instance.fire(BP_EV_START,context);
        bool pass=!instance.vars["Miss"].single.asBool()&&instance.vars["Hit"].single.asBool()&&instance.vars["Actor"].single.asEnt()==targetId;
        snprintf(detail,sizeof(detail),"mask esclusa=%s, mask layer 3=%s, actor=%d/%d",
                 !instance.vars["Miss"].single.asBool()?"ok":"NO",instance.vars["Hit"].single.asBool()?"ok":"NO",
                 instance.vars["Actor"].single.asEnt(),targetId);
        report("Blueprint Trace layer detection",pass,detail);if(!pass)failures++;
    }
    {
        sceneTower(s);
        startBehaviors();
        for (int i = 0; i < 300; i++) s.world.step(FIXED_DT);
        int scattered = 0, total = 0;
        for (auto& e : s.entities) {
            if (e.body->type != BodyType::Dynamic || e.mesh != MESH_CUBE) continue;
            total++;
            if (fabsf(e.body->position.z) > 1 || fabsf(e.body->position.x) > 3) scattered++;
        }
        bool pass = scattered >= 15;
        snprintf(detail, sizeof(detail), "%d/%d mattoni dispersi", scattered, total);
        report("Torre abbattuta", pass, detail);
        if (!pass) failures++;
    }
    {
        scenePendulums(s);
        for (int i = 0; i < 240; i++) s.world.step(FIXED_DT);
        float err = 0;
        for (auto& c : s.world.constraints) {
            float d = c.a->position.distanceTo(c.b->position);
            if (fabsf(d - c.length) > err) err = fabsf(d - c.length);
        }
        bool pass = err < 0.06f;
        snprintf(detail, sizeof(detail), "errore max asta: %.4f m", err);
        report("Pendulum constraints", pass, detail);
        if (!pass) failures++;
    }
    {
        sceneDomino(s);
        for (int i = 0; i < 420; i++) s.world.step(FIXED_DT);
        int fallen = 0, total = 0;
        for (auto& e : s.entities) {
            if (e.body->type != BodyType::Dynamic || e.mesh != MESH_CUBE) continue;
            total++;
            if (e.body->quat.rotate({ 0, 1, 0 }).y < 0.7f) fallen++;
        }
        bool pass = fallen >= total - 2;
        snprintf(detail, sizeof(detail), "%d/%d tessere cadute", fallen, total);
        report("Catena domino", pass, detail);
        if (!pass) failures++;
    }
    {
        sceneDefault(s);
        if (!s.entities.empty()) {
            Entity& audio = s.entities.front();
            audio.hasAudio = true;
            snprintf(audio.audioClip, sizeof(audio.audioClip), "Audio\\musica test.ogg");
            audio.audioVolume = 1.75f;
            audio.audioLoop = true;
            audio.audioSpatial = true;
            audio.audioMinDistance = 2;
            audio.audioMaxDistance = 18;
            std::swap(audio.detailOrder[0], audio.detailOrder[4]);
            audio.detailCollapsed = (1u << DETAIL_MESH) | (1u << DETAIL_AUDIO);
        }
        std::string ser = s.serialize();
        int nEnt = (int)s.entities.size();
        bool ok = s.deserialize(ser);
        std::string ser2 = s.serialize();
        bool audioOk = !s.entities.empty() && s.entities.front().hasAudio && s.entities.front().audioLoop &&
                       strcmp(s.entities.front().audioClip, "Audio\\musica test.ogg") == 0 &&
                       fabsf(s.entities.front().audioVolume - 1.75f) < 1e-4f &&
                       fabsf(audioSourceGain(s.entities.front(), s.entities.front().body->position) - 1.75f) < 1e-4f &&
                       s.entities.front().detailOrder[0] == DETAIL_AUDIO &&
                       (s.entities.front().detailCollapsed & (1u << DETAIL_AUDIO));
        bool pass = ok && (int)s.entities.size() == nEnt && ser == ser2 && audioOk;
        snprintf(detail, sizeof(detail), "%d oggetti, roundtrip %s, Audio Source %s", nEnt,
                 ser == ser2 ? "identico" : "DIFFERENT", audioOk ? "ok" : "NO");
        report("Serializzazione .imp", pass, detail);
        if (!pass) failures++;
    }
    {
        s.clear();
        Entity& empty = s.spawnEmpty("Empty Object", { 2, 3, 4 });
        int emptyId = empty.id;
        std::string ser = s.serialize();
        bool roundtrip = s.deserialize(ser);
        Entity* loaded = s.byId(emptyId);
        bool pass = roundtrip && loaded && loaded->body && !loaded->hasMesh && !loaded->hasPhysics &&
                    !loaded->body->enabled && loaded->scale.x == 1 &&
                    loaded->detailOrder.size() == (size_t)(DETAIL_COMPONENT_COUNT - DETAIL_MESH);
        snprintf(detail, sizeof(detail), "mesh=%d, fisica=%d, transform=(%.0f,%.0f,%.0f), ordine=%d",
                 loaded ? loaded->hasMesh : -1, loaded ? loaded->hasPhysics : -1,
                 loaded ? loaded->body->position.x : 0, loaded ? loaded->body->position.y : 0,
                 loaded ? loaded->body->position.z : 0, loaded ? (int)loaded->detailOrder.size() : 0);
        report("Empty Object e ordine componenti", pass, detail);
        if (!pass) failures++;
    }
    {
        // rotation: Euler round-trip (gimbal-safe range) + forward vector after yaw
        bool rt = true;
        float angs[3][3] = { { 30, 40, 50 }, { -20, 80, 15 }, { 12, -35, 100 } };
        for (auto& a : angs) {
            Quat q = Quat::fromEulerDeg(a[0], a[1], a[2]);
            Vec3 e2 = quatToEulerDeg(q);
            Quat q2 = Quat::fromEulerDeg(e2.x, e2.y, e2.z);
            float dot = q.x * q2.x + q.y * q2.y + q.z * q2.z + q.w * q2.w;
            if (fabsf(dot) < 0.9999f) rt = false;   // same orientation (double cover)
        }
        // fields are (roll, pitch, yaw). Yaw +90° (around up/Y) turns forward (-Z) into -X
        Vec3 fwd = Quat::fromEulerDeg(0, 0, 90).rotate({ 0, 0, -1 });
        bool fwdOk = fabsf(fwd.x + 1) < 1e-3f && fabsf(fwd.y) < 1e-3f && fabsf(fwd.z) < 1e-3f;
        // pitch +90° (around right/X) turns forward (-Z) into +Y
        Vec3 up = Quat::fromEulerDeg(0, 90, 0).rotate({ 0, 0, -1 });
        bool upOk = fabsf(up.y - 1) < 1e-3f && fabsf(up.x) < 1e-3f && fabsf(up.z) < 1e-3f;
        // roll +90° (around forward/Z) turns up (0,1,0) into -X
        Vec3 rup = Quat::fromEulerDeg(90, 0, 0).rotate({ 0, 1, 0 });
        bool rollOk = fabsf(rup.x + 1) < 1e-3f && fabsf(rup.y) < 1e-3f && fabsf(rup.z) < 1e-3f;
        bool pass = rt && fwdOk && upOk && rollOk;
        snprintf(detail, sizeof(detail), "roundtrip %s, yaw %s, pitch %s, roll %s",
                 rt ? "ok" : "NO", fwdOk ? "ok" : "NO", upOk ? "ok" : "NO", rollOk ? "ok" : "NO");
        report("Rotazioni (X=roll Y=pitch Z=yaw)", pass, detail);
        if (!pass) failures++;
    }
    {
        // hierarchy + prefab instantiation (capture ids: refs may be invalidated by growth)
        s.clear();
        int aId = s.spawnBox("padre", { 0, 1, 0 }, { 1, 1, 1 }, {}, BodyType::Dynamic, 1).id;
        int bId = s.spawnBox("figlio", { 2, 1, 0 }, { 1, 1, 1 }, {}, BodyType::Dynamic, 1).id;
        int cId = s.spawnBox("nipote", { 4, 1, 0 }, { 1, 1, 1 }, {}, BodyType::Dynamic, 1).id;
        s.setParent(bId, aId);
        s.setParent(cId, bId);
        bool cycleBlocked = !s.setParent(aId, cId);
        s.moveDescendants(aId, { 1, 0, 0 });
        bool moved = fabsf(s.byId(bId)->body->position.x - 3) < 1e-4f && fabsf(s.byId(cId)->body->position.x - 5) < 1e-4f;
        Quat oldParentRotation = s.byId(aId)->body->quat;
        Quat newParentRotation = Quat::axisAngle({ 0, 1, 0 }, 90.0f * DEG2RAD);
        s.byId(aId)->body->quat = newParentRotation;
        s.rotateDescendants(aId, s.byId(aId)->body->position, oldParentRotation, newParentRotation);
        Vec3 childPos = s.byId(bId)->body->position;
        Vec3 grandchildPos = s.byId(cId)->body->position;
        Vec3 childForward = s.byId(bId)->body->quat.rotate({ 0, 0, -1 });
        bool rotated = fabsf(childPos.x) < 1e-4f && fabsf(childPos.z + 3) < 1e-4f &&
                       fabsf(grandchildPos.x) < 1e-4f && fabsf(grandchildPos.z + 5) < 1e-4f &&
                       fabsf(childForward.x + 1) < 1e-4f;
        Vec3 oldParentScale=s.byId(aId)->scale, halfScale={.5f,.5f,.5f};
        s.byId(aId)->scale=halfScale;
        s.scaleDescendants(aId,s.byId(aId)->body->position,s.byId(aId)->body->quat,oldParentScale,halfScale);
        bool scaled=fabsf(s.byId(bId)->body->position.z+1.5f)<1e-4f&&
                    fabsf(s.byId(cId)->body->position.z+2.5f)<1e-4f&&
                    s.byId(bId)->scale.distanceTo({.5f,.5f,.5f})<1e-4f&&
                    s.byId(cId)->scale.distanceTo({.5f,.5f,.5f})<1e-4f;
        std::vector<int> sub;
        s.collectSubtree(aId, sub);
        std::string pfb = s.serializeSubset(sub);
        std::vector<int> inst = s.instantiateFrom(pfb, { 10, 5, 0 }, true);
        bool instOk = inst.size() == 3;
        int roots = 0;
        for (int id : inst) if (s.byId(id)->parentId == 0) roots++;
        s.removeEntity(aId);
        bool subtreeGone = !s.byId(aId) && !s.byId(bId) && !s.byId(cId);
        bool pass = cycleBlocked && moved && rotated && scaled && instOk && roots == 1 && subtreeGone;
        snprintf(detail, sizeof(detail), "cicli=%s, move/rotate/scale=%s, prefab=%d (%d radici), delete=%s",
                 cycleBlocked ? "ok" : "NO", moved && rotated && scaled ? "ok" : "NO", (int)inst.size(), roots, subtreeGone ? "ok" : "NO");
        report("Gerarchia e prefab", pass, detail);
        if (!pass) failures++;
    }

    {
        // blueprint runtime: EventoInizio → ApplicaImpulso (0,6,0) su massa 1 ⇒ vel.y ≈ 6
        s.clear();
        int cid = s.spawnBox("bp-cubo", { 0, 5, 0 }, { 1, 1, 1 }, {}, BodyType::Dynamic, 1).id;
        BPGraph gph;
        int ev = gph.main().addNode(BP_EV_START, 0, 0);
        int act = gph.main().addNode(BP_ACT_IMPULSE, 100, 0);
        gph.main().byId(act)->lit[1] = { 0, 6, 0 };
        gph.main().connect(ev, 0, act, 0);
        std::string ser = gph.serialize();
        BPGraph g2;
        bool serOk = g2.deserialize(ser) && g2.serialize() == ser && g2.main().nodes.size() == 2 && g2.main().links.size() == 1;
        BPInstance inst;
        inst.graph = &g2;
        inst.entity = s.byId(cid);
        inst.initVars(nullptr);
        BPContext ctx;
        ctx.entity = inst.entity;
        ctx.scene = &s;
        inst.fire(BP_EV_START, ctx);
        float vy = s.byId(cid)->body->velocity.y;
        bool pass = serOk && fabsf(vy - 6.0f) < 1e-4f;
        snprintf(detail, sizeof(detail), "roundtrip %s, impulso via grafo: vel.y=%.3f (atteso 6)", serOk ? "ok" : "NO", vy);
        report("Blueprint runtime", pass, detail);
        if (!pass) failures++;
    }
    {
        // blueprint v2: variabili, funzione con ritorno, ForLoop+array, evento custom via messaggio
        s.clear();
        int cid = s.spawnBox("bp2", { 0, 5, 0 }, { 1, 1, 1 }, {}, BodyType::Dynamic, 1).id;
        BPGraph gph;
        auto addVar = [&](const char* nm, VarContainer c) {
            BPVarDef v;
            snprintf(v.name, sizeof(v.name), "%s", nm);
            v.container = c;
            gph.vars.push_back(v);
        };
        addVar("conta", VC_SINGLE);
        addVar("lista", VC_ARRAY);
        addVar("somma", VC_SINGLE);
        addVar("segnale", VC_SINGLE);
        // funzione raddoppia(p1) → p1 * 2
        BPFunc fn;
        snprintf(fn.name, sizeof(fn.name), "raddoppia");
        int fe = fn.body.addNode(BP_FN_ENTRY, 0, 0);
        int fm = fn.body.addNode(BP_M_MUL, 100, 0);
        fn.body.byId(fm)->lit[1] = { 2, 0, 0 };
        int fr = fn.body.addNode(BP_FN_RETURN, 200, 0);
        fn.body.connect(fe, 1, fm, 0);   // p1 → A
        fn.body.connect(fe, 0, fr, 0);   // exec
        fn.body.connect(fm, 0, fr, 1);   // mul → valore
        gph.funcs.push_back(fn);
        // main: Start → set conta=5 → chiama raddoppia(conta) → set conta=ritorno
        //       → For 1..3 { lista.aggiungi(indice) } → foreach { somma += elemento } → chiama evento "ping"
        BPCanvas& m = gph.main();
        int ev = m.addNode(BP_EV_START, 0, 0);
        int set5 = m.addNode(BP_VAR_SET, 0, 0);
        snprintf(m.byId(set5)->sname, 32, "conta");
        m.byId(set5)->lit[1] = { 5, 0, 0 };
        int call = m.addNode(BP_CALL_FUNC, 0, 0);
        snprintf(m.byId(call)->sname, 32, "raddoppia");
        int getC = m.addNode(BP_VAR_GET, 0, 0);
        snprintf(m.byId(getC)->sname, 32, "conta");
        int setC = m.addNode(BP_VAR_SET, 0, 0);
        snprintf(m.byId(setC)->sname, 32, "conta");
        int forN = m.addNode(BP_FLOW_FOR, 0, 0);
        m.byId(forN)->lit[1] = { 1, 0, 0 };
        m.byId(forN)->lit[2] = { 3, 0, 0 };
        int arrAdd = m.addNode(BP_ARR_ADD, 0, 0);
        snprintf(m.byId(arrAdd)->sname, 32, "lista");
        int fe2 = m.addNode(BP_FLOW_FOREACH, 0, 0);
        snprintf(m.byId(fe2)->sname, 32, "lista");
        int sumSet = m.addNode(BP_VAR_SET, 0, 0);
        snprintf(m.byId(sumSet)->sname, 32, "somma");
        int add = m.addNode(BP_M_ADD, 0, 0);
        int getSum = m.addNode(BP_VAR_GET, 0, 0);
        snprintf(m.byId(getSum)->sname, 32, "somma");
        int callEv = m.addNode(BP_CALL_EVENT, 0, 0);
        snprintf(m.byId(callEv)->sname, 32, "ping");
        int evc = m.addNode(BP_EV_CUSTOM, 0, 0);
        snprintf(m.byId(evc)->sname, 32, "ping");
        int setSig = m.addNode(BP_VAR_SET, 0, 0);
        snprintf(m.byId(setSig)->sname, 32, "segnale");
        m.byId(setSig)->lit[1] = { 42, 0, 0 };
        m.connect(ev, 0, set5, 0);
        m.connect(set5, 0, call, 0);
        m.connect(getC, 0, call, 1);
        m.connect(call, 0, setC, 0);
        m.connect(call, 1, setC, 1);
        m.connect(setC, 0, forN, 0);
        m.connect(forN, 0, arrAdd, 0);   // corpo
        m.connect(forN, 1, arrAdd, 1);   // indice → valore
        m.connect(forN, 2, fe2, 0);      // fine → foreach
        m.connect(fe2, 0, sumSet, 0);    // corpo
        m.connect(getSum, 0, add, 0);
        m.connect(fe2, 1, add, 1);       // elemento
        m.connect(add, 0, sumSet, 1);
        m.connect(fe2, 3, callEv, 0);    // fine → chiama evento
        m.connect(evc, 0, setSig, 0);
        // roundtrip + run
        std::string ser = gph.serialize();
        BPGraph g2;
        bool serOk = g2.deserialize(ser) && g2.serialize() == ser && g2.funcs.size() == 1 && g2.vars.size() == 4;
        BPInstance inst;
        inst.graph = &g2;
        inst.entity = s.byId(cid);
        inst.initVars(nullptr);
        BPContext ctx;
        ctx.entity = inst.entity;
        ctx.scene = &s;
        inst.fire(BP_EV_START, ctx);
        float conta = inst.vars["conta"].single.asNum();
        int nArr = (int)inst.vars["lista"].arr.size();
        float somma = inst.vars["somma"].single.asNum();
        float segnale = inst.vars["segnale"].single.asNum();
        bool pass = serOk && fabsf(conta - 10) < 1e-4f && nArr == 3 && fabsf(somma - 6) < 1e-4f && fabsf(segnale - 42) < 1e-4f;
        snprintf(detail, sizeof(detail), "roundtrip %s, fn: conta=%g (10), for: %d el (3), foreach: somma=%g (6), evento: %g (42)",
                 serOk ? "ok" : "NO", conta, nArr, somma, segnale);
        report("Blueprint v2 completo", pass, detail);
        if (!pass) failures++;
    }

    {
        // blueprint v3: variabile Transform, string inline (slit), Set con pin di ritorno, nodi nuovi
        s.clear();
        int cid = s.spawnBox("bp3", { 0, 5, 0 }, { 1, 1, 1 }, {}, BodyType::Dynamic, 1).id;
        BPGraph gph;
        BPVarDef vt; vt.type = PIN_TRANSFORM; snprintf(vt.name, sizeof(vt.name), "spawn");
        vt.def = { 1, 2, 3 }; vt.defRot = { 0, 45, 0 }; vt.defScl = { 2, 2, 2 }; gph.vars.push_back(vt);
        BPVarDef vs; vs.type = PIN_STR; snprintf(vs.name, sizeof(vs.name), "etichetta"); snprintf(vs.strDef, sizeof(vs.strDef), "ciao"); gph.vars.push_back(vs);
        BPVarDef vn; snprintf(vn.name, sizeof(vn.name), "eco"); gph.vars.push_back(vn);        // Float
        BPVarDef vn2; snprintf(vn2.name, sizeof(vn2.name), "copia"); gph.vars.push_back(vn2);  // Float
        BPCanvas& m = gph.main();
        int ev = m.addNode(BP_EV_START, 0, 0);
        int setS = m.addNode(BP_VAR_SET, 0, 0); snprintf(m.byId(setS)->sname, 32, "etichetta"); m.byId(setS)->slit[1] = "mondo";
        int setE = m.addNode(BP_VAR_SET, 0, 0); snprintf(m.byId(setE)->sname, 32, "eco"); m.byId(setE)->lit[1] = { 7, 0, 0 };
        int setCp = m.addNode(BP_VAR_SET, 0, 0); snprintf(m.byId(setCp)->sname, 32, "copia");
        m.addNode(BP_MAKE_TF, 0, 0);   // copertura serializzazione nodi nuovi
        m.addNode(BP_AUDIO_PLAY, 0, 0);
        m.addNode(BP_AUDIO_STOP, 0, 0);
        m.addNode(BP_AUDIO_SET_VOLUME, 0, 0);
        m.addNode(BP_AUDIO_SET_CLIP, 0, 0);
        m.addNode(BP_AUDIO_FADE_IN, 0, 0);
        m.addNode(BP_AUDIO_FADE_OUT, 0, 0);
        int sp = m.addNode(BP_SET_PHYSTYPE, 0, 0); m.byId(sp)->choice = 1;   // Statica
        m.connect(ev, 0, setS, 0);
        m.connect(setS, 0, setE, 0);
        m.connect(setE, 0, setCp, 0);
        m.connect(setE, 1, setCp, 1);   // pin di ritorno del Set → valore di un altro Set
        m.connect(setCp, 0, sp, 0);
        std::string ser = gph.serialize();
        BPGraph g2;
        bool serOk = g2.deserialize(ser) && g2.serialize() == ser && g2.vars.size() == 4;
        BPVarDef* tv = g2.findVar("spawn");
        bool tfOk = tv && tv->type == PIN_TRANSFORM && fabsf(tv->defRot.y - 45) < 1e-4f && fabsf(tv->defScl.x - 2) < 1e-4f;
        BPNode* sn = g2.main().byId(setS);
        bool slitOk = sn && sn->slit[1] == "mondo";
        BPInstance inst; inst.graph = &g2; inst.entity = s.byId(cid); inst.initVars(nullptr);
        BPContext ctx; ctx.entity = inst.entity; ctx.scene = &s;
        inst.fire(BP_EV_START, ctx);
        std::string eco = inst.vars["etichetta"].single.str;
        float copia = inst.vars["copia"].single.asNum();
        bool statico = s.byId(cid)->body->type == BodyType::Static;
        bool pass = serOk && tfOk && slitOk && eco == "mondo" && fabsf(copia - 7) < 1e-4f && statico;
        snprintf(detail, sizeof(detail), "roundtrip %s, transform %s, slit %s, etichetta=%s (mondo), copia=%g (7), fisica=%s",
                 serOk ? "ok" : "NO", tfOk ? "ok" : "NO", slitOk ? "ok" : "NO", eco.c_str(), copia, statico ? "statica" : "dinamica");
        report("Blueprint v3 (transform/string/set-ritorno)", pass, detail);
        if (!pass) failures++;
    }

    {
        // blueprint v4: custom event con parametro + comment (roundtrip + runtime)
        s.clear();
        int cid = s.spawnBox("bp4", { 0, 5, 0 }, { 1, 1, 1 }, {}, BodyType::Dynamic, 1).id;
        BPGraph gph;
        BPVarDef vn; snprintf(vn.name, sizeof(vn.name), "eco"); gph.vars.push_back(vn);
        BPEventDef edf; snprintf(edf.name, sizeof(edf.name), "eco_ev");
        BPFuncPin ep; snprintf(ep.name, sizeof(ep.name), "n"); ep.kind = PIN_NUM; edf.params.push_back(ep);
        gph.events.push_back(edf);
        BPCanvas& m = gph.main();
        int ev = m.addNode(BP_EV_START, 0, 0);
        int callev = m.addNode(BP_CALL_EVENT, 0, 0); snprintf(m.byId(callev)->sname, 32, "eco_ev");
        m.byId(callev)->lit[1] = { 9, 0, 0 };   // param n = 9 (literal)
        int cev = m.addNode(BP_EV_CUSTOM, 0, 0); snprintf(m.byId(cev)->sname, 32, "eco_ev");
        int setEco = m.addNode(BP_VAR_SET, 0, 0); snprintf(m.byId(setEco)->sname, 32, "eco");
        m.connect(ev, 0, callev, 0);
        m.connect(cev, 0, setEco, 0);
        m.connect(cev, 1, setEco, 1);   // param n dell'evento → valore del Set
        BPComment cmt; cmt.x = 5; cmt.y = 5; snprintf(cmt.text, sizeof(cmt.text), "test v4"); m.comments.push_back(cmt);
        std::string ser = gph.serialize();
        BPGraph g2;
        bool serOk = g2.deserialize(ser) && g2.serialize() == ser && g2.events.size() == 1 && g2.main().comments.size() == 1;
        bool evOk = !g2.events.empty() && g2.events[0].params.size() == 1;
        BPInstance inst; inst.graph = &g2; inst.entity = s.byId(cid); inst.initVars(nullptr);
        BPContext ctx; ctx.entity = inst.entity; ctx.scene = &s;
        inst.fire(BP_EV_START, ctx);
        float ecoVal = inst.vars["eco"].single.asNum();
        bool pass = serOk && evOk && fabsf(ecoVal - 9) < 1e-4f;
        snprintf(detail, sizeof(detail), "roundtrip %s, evento(param) %s, comment %d, eco=%g (9)",
                 serOk ? "ok" : "NO", evOk ? "ok" : "NO", (int)g2.main().comments.size(), ecoVal);
        report("Blueprint v4 (custom event param + comment)", pass, detail);
        if (!pass) failures++;
    }

    {
        // blueprint v5: bind di un custom event a un dispatcher (pin delegate) + Call Dispatcher
        s.clear();
        int cid = s.spawnBox("bp5", { 0, 5, 0 }, { 1, 1, 1 }, {}, BodyType::Dynamic, 1).id;
        BPGraph gph;
        BPVarDef vn; snprintf(vn.name, sizeof(vn.name), "colpito"); gph.vars.push_back(vn);
        BPDispatcherDef dispatcher;snprintf(dispatcher.name,sizeof(dispatcher.name),"OnHit");
        BPFuncPin damagePin;snprintf(damagePin.name,sizeof(damagePin.name),"Damage");damagePin.kind=PIN_NUM;
        dispatcher.params.push_back(damagePin);gph.dispatchers.push_back(dispatcher);
        BPEventDef reactSignature;snprintf(reactSignature.name,sizeof(reactSignature.name),"reagisci");
        reactSignature.params.push_back(damagePin);gph.events.push_back(reactSignature);
        BPCanvas& m = gph.main();
        int ev = m.addNode(BP_EV_START, 0, 0);
        int cev = m.addNode(BP_EV_CUSTOM, 0, 0); snprintf(m.byId(cev)->sname, 32, "reagisci");
        int setC = m.addNode(BP_VAR_SET, 0, 0); snprintf(m.byId(setC)->sname, 32, "colpito");
        m.connect(cev, 0, setC, 0);
        m.connect(cev, 1, setC, 1);
        int bind = m.addNode(BP_BIND_EVENT, 0, 0); snprintf(m.byId(bind)->sname, 32, "OnHit");
        m.connect(cev, 2, bind, 1);   // exec, Damage, Delegate
        int cd = m.addNode(BP_CALL_DISPATCH, 0, 0); snprintf(m.byId(cd)->sname, 32, "OnHit");
        m.byId(cd)->lit[1].x=13;
        m.connect(ev, 0, bind, 0);    // Start → Bind
        m.connect(bind, 0, cd, 0);    // Bind → Call Dispatcher
        std::string ser = gph.serialize();
        BPGraph g2;
        bool serOk = g2.deserialize(ser) && g2.serialize() == ser && g2.dispatchers.size() == 1 &&
                     g2.dispatchers[0].params.size()==1;
        BPInstance inst; inst.graph = &g2; inst.entity = s.byId(cid); inst.initVars(nullptr);
        BPContext ctx; ctx.entity = inst.entity; ctx.scene = &s;
        inst.fire(BP_EV_START, ctx);
        float colpito = inst.vars["colpito"].single.asNum();
        bool pass = serOk && fabsf(colpito - 13) < 1e-4f;
        snprintf(detail, sizeof(detail), "roundtrip %s, firma + broadcast argomento: colpito=%g (13)", serOk ? "ok" : "NO", colpito);
        report("Blueprint v5 (delegate / dispatcher)", pass, detail);
        if (!pass) failures++;
    }

    {
        // blueprint v6: una variabile Transform esposta e' un VALORE esplicito
        // (Location/Rotation/Scale), non un binding a un oggetto della scena.
        // Gli override per-istanza usano le chiavi <nome>, <nome>#rot e <nome>#scl.
        s.clear();
        int owner = s.spawnBox("owner", { 0, 5, 0 }, { 1, 1, 1 }, {}, BodyType::Dynamic, 1).id;
        BPGraph gph;
        BPVarDef vt; vt.type = PIN_TRANSFORM; vt.scope = VS_PUBLIC; vt.expose = true;
        vt.def = { 1, 2, 3 }; vt.defRot = { 0, 90, 0 }; vt.defScl = { 2, 2, 2 };
        snprintf(vt.name, sizeof(vt.name), "punto"); gph.vars.push_back(vt);
        BPVarDef vc; vc.type = PIN_ENT; vc.scope = VS_PUBLIC; vc.expose = true;
        snprintf(vc.name, sizeof(vc.name), "cameraRef");
        snprintf(vc.refClass, sizeof(vc.refClass), "component:Camera");
        gph.vars.push_back(vc);
        BPGraph roundtrip;
        std::string refSer = gph.serialize();
        bool classRoundtrip = roundtrip.deserialize(refSer) && roundtrip.vars.size() == 2 &&
                              strcmp(roundtrip.vars[1].refClass, "component:Camera") == 0 &&
                              (roundtrip.vars[0].defRot - Vec3{ 0, 90, 0 }).length() < 1e-4f &&
                              (roundtrip.vars[0].defScl - Vec3{ 2, 2, 2 }).length() < 1e-4f;
        // senza override: valgono i default della definizione
        BPInstance plain; plain.graph = &gph; plain.entity = s.byId(owner);
        plain.initVars(nullptr);
        BPValue dv = plain.vars["punto"].single;
        bool defaultsOk = dv.kind == PIN_TRANSFORM &&
                          (dv.vec - Vec3{ 1, 2, 3 }).length() < 1e-4f &&
                          (dv.rot - Vec3{ 0, 90, 0 }).length() < 1e-4f &&
                          (dv.scl - Vec3{ 2, 2, 2 }).length() < 1e-4f;
        // con override: le tre componenti arrivano dalle rispettive chiavi
        BPInstance inst; inst.graph = &gph; inst.entity = s.byId(owner);
        std::map<std::string, Vec3> ov;
        ov["punto"] = { 4, 2, -3 };
        ov["punto#rot"] = { 0, 45, 0 };
        ov["punto#scl"] = { 3, 3, 3 };
        inst.initVars(&ov);
        inst.applyRefOverrides(&ov, &s);   // no-op: non deve sovrascrivere il valore
        BPValue pv = inst.vars["punto"].single;
        bool overrideOk = pv.kind == PIN_TRANSFORM &&
                          (pv.vec - Vec3{ 4, 2, -3 }).length() < 1e-4f &&
                          (pv.rot - Vec3{ 0, 45, 0 }).length() < 1e-4f &&
                          (pv.scl - Vec3{ 3, 3, 3 }).length() < 1e-4f;
        bool ok = classRoundtrip && defaultsOk && overrideOk;
        snprintf(detail, sizeof(detail), "roundtrip %s, default %s, override pos=(%.1f,%.1f,%.1f) rot=(%.1f,%.1f,%.1f) scala=(%.1f,%.1f,%.1f)",
                 classRoundtrip ? "ok" : "NO", defaultsOk ? "ok" : "NO",
                 pv.vec.x, pv.vec.y, pv.vec.z, pv.rot.x, pv.rot.y, pv.rot.z, pv.scl.x, pv.scl.y, pv.scl.z);
        report("Blueprint v6 (Transform esposto = valore esplicito)", ok, detail);
        if (!ok) failures++;
    }

    {
        // blueprint v7: Timer Handle persistente, pausa/ripresa e azioni Delay latenti
        s.clear();
        int owner = s.spawnBox("timer_owner", { 0, 2, 0 }, { 1, 1, 1 }, {}, BodyType::Static).id;

        BPGraph timerGraph;
        BPVarDef vh; vh.type = PIN_TIMER_HANDLE; snprintf(vh.name, sizeof(vh.name), "timer"); timerGraph.vars.push_back(vh);
        BPVarDef vf; vf.type = PIN_NUM; snprintf(vf.name, sizeof(vf.name), "scaduto"); timerGraph.vars.push_back(vf);
        BPCanvas& tm = timerGraph.main();
        int start = tm.addNode(BP_EV_START, 0, 0);
        int setTimer = tm.addNode(BP_TIMER_SET, 0, 0); tm.byId(setTimer)->lit[1].x = 0.06f;
        int saveHandle = tm.addNode(BP_VAR_SET, 0, 0); snprintf(tm.byId(saveHandle)->sname, 32, "timer");
        int markDone = tm.addNode(BP_VAR_SET, 0, 0); snprintf(tm.byId(markDone)->sname, 32, "scaduto"); tm.byId(markDone)->lit[1].x = 1;
        tm.connect(start, 0, setTimer, 0);
        tm.connect(setTimer, 0, saveHandle, 0);       // Started, immediato
        tm.connect(setTimer, 2, saveHandle, 1);       // Handle → variabile Timer Handle
        tm.connect(setTimer, 1, markDone, 0);         // Completed, alla scadenza

        int pauseEv = tm.addNode(BP_EV_CUSTOM, 0, 0); snprintf(tm.byId(pauseEv)->sname, 32, "PausaTimer");
        int pause = tm.addNode(BP_TIMER_PAUSE, 0, 0);
        int getForPause = tm.addNode(BP_VAR_GET, 0, 0); snprintf(tm.byId(getForPause)->sname, 32, "timer");
        tm.connect(pauseEv, 0, pause, 0); tm.connect(getForPause, 0, pause, 1);
        int resumeEv = tm.addNode(BP_EV_CUSTOM, 0, 0); snprintf(tm.byId(resumeEv)->sname, 32, "RiprendiTimer");
        int resume = tm.addNode(BP_TIMER_UNPAUSE, 0, 0);
        int getForResume = tm.addNode(BP_VAR_GET, 0, 0); snprintf(tm.byId(getForResume)->sname, 32, "timer");
        tm.connect(resumeEv, 0, resume, 0); tm.connect(getForResume, 0, resume, 1);

        std::string timerSer = timerGraph.serialize();
        BPGraph timerRoundtrip;
        bool serOk = timerRoundtrip.deserialize(timerSer) && timerRoundtrip.serialize() == timerSer &&
                     timerRoundtrip.vars[0].type == PIN_TIMER_HANDLE;
        BPInstance timerInst; timerInst.graph = &timerRoundtrip; timerInst.entity = s.byId(owner); timerInst.initVars(nullptr);
        BPContext tctx; tctx.entity = timerInst.entity; tctx.scene = &s; tctx.dt = 0.02f;
        timerInst.fire(BP_EV_START, tctx);
        int savedHandle = timerInst.vars["timer"].single.asTimerHandle();
        timerInst.fireCustom("PausaTimer", tctx);
        for (int i = 0; i < 5; i++) timerInst.fire(BP_EV_TICK, tctx);
        bool stayedPaused = timerInst.vars["scaduto"].single.asNum() == 0;
        timerInst.fireCustom("RiprendiTimer", tctx);
        for (int i = 0; i < 4; i++) timerInst.fire(BP_EV_TICK, tctx);
        bool resumed = timerInst.vars["scaduto"].single.asNum() == 1;

        BPGraph delayGraph;
        BPVarDef vd; vd.type = PIN_NUM; snprintf(vd.name, sizeof(vd.name), "delayDone"); delayGraph.vars.push_back(vd);
        BPVarDef vr; vr.type = PIN_NUM; snprintf(vr.name, sizeof(vr.name), "retriggerDone"); delayGraph.vars.push_back(vr);
        BPCanvas& dm = delayGraph.main();
        int ds = dm.addNode(BP_EV_START, 0, 0);
        int delay = dm.addNode(BP_FLOW_DELAY, 0, 0); dm.byId(delay)->lit[1].x = 0.04f;
        int delayDone = dm.addNode(BP_VAR_SET, 0, 0); snprintf(dm.byId(delayDone)->sname, 32, "delayDone"); dm.byId(delayDone)->lit[1].x = 1;
        dm.connect(ds, 0, delay, 0); dm.connect(delay, 0, delayDone, 0);
        int re = dm.addNode(BP_EV_CUSTOM, 0, 0); snprintf(dm.byId(re)->sname, 32, "Retrigger");
        int retrigger = dm.addNode(BP_FLOW_RETRIGGER_DELAY, 0, 0); dm.byId(retrigger)->lit[1].x = 0.04f;
        int retriggerDone = dm.addNode(BP_VAR_SET, 0, 0); snprintf(dm.byId(retriggerDone)->sname, 32, "retriggerDone"); dm.byId(retriggerDone)->lit[1].x = 1;
        dm.connect(re, 0, retrigger, 0); dm.connect(retrigger, 0, retriggerDone, 0);
        BPInstance delayInst; delayInst.graph = &delayGraph; delayInst.entity = s.byId(owner); delayInst.initVars(nullptr);
        delayInst.fire(BP_EV_START, tctx);
        delayInst.fire(BP_EV_TICK, tctx);
        delayInst.fire(BP_EV_START, tctx);             // Delay normale: ignorato, non riparte
        delayInst.fire(BP_EV_TICK, tctx);
        bool normalDelay = delayInst.vars["delayDone"].single.asNum() == 1;
        delayInst.fireCustom("Retrigger", tctx);
        delayInst.fire(BP_EV_TICK, tctx);
        delayInst.fireCustom("Retrigger", tctx);       // riparte da 0.04
        delayInst.fire(BP_EV_TICK, tctx);
        bool notEarly = delayInst.vars["retriggerDone"].single.asNum() == 0;
        delayInst.fire(BP_EV_TICK, tctx);
        bool retriggerOk = delayInst.vars["retriggerDone"].single.asNum() == 1;

        bool ok = serOk && savedHandle > 0 && stayedPaused && resumed && normalDelay && notEarly && retriggerOk;
        snprintf(detail, sizeof(detail), "roundtrip %s, handle=%d, pausa=%s, ripresa=%s, delay=%s, retrigger=%s",
                 serOk ? "ok" : "NO", savedHandle, stayedPaused ? "ok" : "NO", resumed ? "ok" : "NO",
                 normalDelay ? "ok" : "NO", (notEarly && retriggerOk) ? "ok" : "NO");
        report("Blueprint v7 (timer / delay latenti)", ok, detail);
        if (!ok) failures++;
    }

    {
        // Blueprint v8: Curve asset roundtrip + Evaluate Curve node runtime.
        CurveAsset curve;
        curve.interp = CURVE_LINEAR;
        curve.keys = { { 0, 0 }, { 1, 2 }, { 2, 0 } };
        CurveAsset roundtrip;
        std::string curveSer = curve.serialize();
        bool assetOk = roundtrip.deserialize(curveSer) && fabsf(roundtrip.evaluate(0.25f) - 0.5f) < 1e-4f &&
                       fabsf(roundtrip.evaluate(1.5f) - 1.0f) < 1e-4f;
        CurveAsset shaped;
        shaped.interp = CURVE_SMOOTH;
        shaped.keys = { { 0, 0 }, { 1, 1 } };
        shaped.keys[0].tangentUser = shaped.keys[1].tangentUser = true;
        shaped.keys[0].arriveTangent = shaped.keys[0].leaveTangent = 0;
        shaped.keys[1].arriveTangent = shaped.keys[1].leaveTangent = 0;
        CurveAsset shapedRoundtrip;
        bool tangentOk = shapedRoundtrip.deserialize(shaped.serialize()) && shapedRoundtrip.keys[0].tangentUser &&
                         fabsf(shapedRoundtrip.evaluate(0.25f) - 0.15625f) < 1e-4f;
        CurveAsset legacy;
        bool legacyOk = legacy.deserialize("IMPULSOCURVE 1\ninterp 1\nkey 0 0\nkey 1 1\n") &&
                        fabsf(legacy.evaluate(0.25f) - 0.25f) < 1e-4f;
        assetOk = assetOk && tangentOk && legacyOk;

        s.clear();
        int owner = s.spawnBox("curve_owner", { 0, 1, 0 }, { 1, 1, 1 }, {}, BodyType::Static).id;
        BPGraph graph;
        BPVarDef vv; snprintf(vv.name, sizeof(vv.name), "risultato"); graph.vars.push_back(vv);
        BPCanvas& cv = graph.main();
        int start = cv.addNode(BP_EV_START, 0, 0);
        int eval = cv.addNode(BP_CURVE_EVAL, 0, 0);
        snprintf(cv.byId(eval)->sname, sizeof(cv.byId(eval)->sname), "Animazioni\\Movimento.curve");
        cv.byId(eval)->lit[0].x = 0.25f;
        int set = cv.addNode(BP_VAR_SET, 0, 0); snprintf(cv.byId(set)->sname, sizeof(cv.byId(set)->sname), "risultato");
        cv.connect(start, 0, set, 0);
        cv.connect(eval, 0, set, 1);
        std::string bpSer = graph.serialize();
        BPGraph graph2;
        bool bpRoundtrip = graph2.deserialize(bpSer) && graph2.serialize() == bpSer;
        BPInstance inst; inst.graph = &graph2; inst.entity = s.byId(owner); inst.initVars(nullptr);
        BPContext ctx; ctx.entity = inst.entity; ctx.scene = &s;
        ctx.evalCurve = +[](const char* path, float t) -> float {
            return path && strcmp(path, "Animazioni\\Movimento.curve") == 0 ? t * 4.0f : 0.0f;
        };
        inst.fire(BP_EV_START, ctx);
        float result = inst.vars["risultato"].single.asNum();
        bool ok = assetOk && bpRoundtrip && fabsf(result - 1.0f) < 1e-4f;
        snprintf(detail, sizeof(detail), "asset=%s, path roundtrip=%s, evaluate=%g (1)",
                 assetOk ? "ok" : "NO", bpRoundtrip ? "ok" : "NO", result);
        report("Blueprint v8 (Curve asset / Evaluate Curve)", ok, detail);
        if (!ok) failures++;
    }

    {
        // Blueprint v9: Timer validity, callback delegate e callback funzione diretta.
        s.clear();
        int owner = s.spawnBox("timer_v2_owner", { 0, 1, 0 }, { 1, 1, 1 }, {}, BodyType::Static).id;
        BPGraph graph;
        BPVarDef vh; vh.type = PIN_TIMER_HANDLE; snprintf(vh.name, sizeof(vh.name), "handle"); graph.vars.push_back(vh);
        BPVarDef vv; vv.type = PIN_BOOL; snprintf(vv.name, sizeof(vv.name), "valid"); graph.vars.push_back(vv);
        BPVarDef ve; ve.type = PIN_BOOL; snprintf(ve.name, sizeof(ve.name), "eventFired"); graph.vars.push_back(ve);
        BPVarDef vf; vf.type = PIN_BOOL; snprintf(vf.name, sizeof(vf.name), "functionFired"); graph.vars.push_back(vf);
        BPCanvas& cv = graph.main();

        int start = cv.addNode(BP_EV_START, 0, 0);
        int setEventTimer = cv.addNode(BP_TIMER_SET, 0, 0); cv.byId(setEventTimer)->lit[1].x = 0.04f;
        int saveHandle = cv.addNode(BP_VAR_SET, 0, 0); snprintf(cv.byId(saveHandle)->sname, sizeof(cv.byId(saveHandle)->sname), "handle");
        cv.connect(start, 0, setEventTimer, 0);
        cv.connect(setEventTimer, 0, saveHandle, 0);
        cv.connect(setEventTimer, 2, saveHandle, 1);

        int callback = cv.addNode(BP_EV_CUSTOM, 0, 0); snprintf(cv.byId(callback)->sname, sizeof(cv.byId(callback)->sname), "TimerCallback");
        int markEvent = cv.addNode(BP_VAR_SET, 0, 0); snprintf(cv.byId(markEvent)->sname, sizeof(cv.byId(markEvent)->sname), "eventFired"); cv.byId(markEvent)->lit[1].x = 1;
        cv.connect(callback, 0, markEvent, 0);
        cv.connect(callback, 1, setEventTimer, 3); // delegate nell'header -> input Event

        int check = cv.addNode(BP_EV_CUSTOM, 0, 0); snprintf(cv.byId(check)->sname, sizeof(cv.byId(check)->sname), "CheckTimer");
        int getHandle = cv.addNode(BP_VAR_GET, 0, 0); snprintf(cv.byId(getHandle)->sname, sizeof(cv.byId(getHandle)->sname), "handle");
        int isValid = cv.addNode(BP_TIMER_IS_VALID, 0, 0);
        int saveValid = cv.addNode(BP_VAR_SET, 0, 0); snprintf(cv.byId(saveValid)->sname, sizeof(cv.byId(saveValid)->sname), "valid");
        cv.connect(check, 0, saveValid, 0);
        cv.connect(getHandle, 0, isValid, 0);
        cv.connect(isValid, 0, saveValid, 1);

        BPFunc fn;
        snprintf(fn.name, sizeof(fn.name), "OnTimerFunction");
        fn.ins.clear(); fn.outs.clear();
        int entry = fn.body.addNode(BP_FN_ENTRY, 0, 0);
        int markFunction = fn.body.addNode(BP_VAR_SET, 0, 0); snprintf(fn.body.byId(markFunction)->sname, sizeof(fn.body.byId(markFunction)->sname), "functionFired"); fn.body.byId(markFunction)->lit[1].x = 1;
        fn.body.connect(entry, 0, markFunction, 0);
        graph.funcs.push_back(fn);
        int startFn = cv.addNode(BP_EV_START, 0, 0);
        int setFunctionTimer = cv.addNode(BP_TIMER_SET_FUNC, 0, 0); cv.byId(setFunctionTimer)->lit[1].x = 0.02f;
        snprintf(cv.byId(setFunctionTimer)->sname, sizeof(cv.byId(setFunctionTimer)->sname), "OnTimerFunction");
        cv.connect(startFn, 0, setFunctionTimer, 0);

        std::string ser = graph.serialize();
        BPGraph roundtrip;
        bool serOk = roundtrip.deserialize(ser) && roundtrip.serialize() == ser;
        BPInstance inst; inst.graph = &roundtrip; inst.entity = s.byId(owner); inst.initVars(nullptr);
        BPContext ctx; ctx.entity = inst.entity; ctx.scene = &s; ctx.dt = 0.02f;
        inst.fireCustom("CheckTimer", ctx);
        bool emptyInvalid = !inst.vars["valid"].single.asBool();
        inst.fire(BP_EV_START, ctx);
        inst.fireCustom("CheckTimer", ctx);
        bool activeValid = inst.vars["valid"].single.asBool();
        inst.fire(BP_EV_TICK, ctx);
        bool fnFired = inst.vars["functionFired"].single.asBool();
        inst.fire(BP_EV_TICK, ctx);
        inst.fireCustom("CheckTimer", ctx);
        bool eventFired = inst.vars["eventFired"].single.asBool();
        bool expiredInvalid = !inst.vars["valid"].single.asBool();
        bool ok = serOk && emptyInvalid && activeValid && fnFired && eventFired && expiredInvalid;
        snprintf(detail, sizeof(detail), "roundtrip=%s, vuoto=%s, attivo=%s, delegate=%s, funzione=%s, scaduto=%s",
                 serOk ? "ok" : "NO", emptyInvalid ? "ok" : "NO", activeValid ? "ok" : "NO",
                 eventFired ? "ok" : "NO", fnFired ? "ok" : "NO", expiredInvalid ? "ok" : "NO");
        report("Blueprint v9 (Timer valid / delegate / function ref)", ok, detail);
        if (!ok) failures++;
    }

    {
        // Blueprint v10: a pure function has data pins only and is evaluated
        // lazily when another node asks for its output.
        s.clear();
        int owner = s.spawnBox("pure_function_owner", { 0, 1, 0 }, { 1, 1, 1 }, {}, BodyType::Static).id;
        BPGraph graph;
        BPVarDef resultDef; resultDef.type = PIN_NUM; snprintf(resultDef.name, sizeof(resultDef.name), "risultato");
        graph.vars.push_back(resultDef);

        BPFunc fn;
        snprintf(fn.name, sizeof(fn.name), "SommaPura");
        fn.pure = true;
        BPFuncPin a; snprintf(a.name, sizeof(a.name), "A"); a.kind = PIN_NUM; fn.ins.push_back(a);
        BPFuncPin b; snprintf(b.name, sizeof(b.name), "B"); b.kind = PIN_NUM; fn.ins.push_back(b);
        BPFuncPin value; snprintf(value.name, sizeof(value.name), "Value"); value.kind = PIN_NUM; fn.outs.push_back(value);
        int entry = fn.body.addNode(BP_FN_ENTRY, 0, 0);
        int add = fn.body.addNode(BP_M_ADD, 120, 0);
        int ret = fn.body.addNode(BP_FN_RETURN, 260, 0);
        fn.body.connect(entry, 0, add, 0);
        fn.body.connect(entry, 1, add, 1);
        fn.body.connect(add, 0, ret, 0);
        graph.funcs.push_back(fn);

        BPCanvas& cv = graph.main();
        int start = cv.addNode(BP_EV_START, 0, 0);
        int call = cv.addNode(BP_CALL_FUNC, 100, 0);
        snprintf(cv.byId(call)->sname, sizeof(cv.byId(call)->sname), "SommaPura");
        cv.byId(call)->lit[0].x = 2;
        cv.byId(call)->lit[1].x = 3;
        int set = cv.addNode(BP_VAR_SET, 260, 0);
        snprintf(cv.byId(set)->sname, sizeof(cv.byId(set)->sname), "risultato");
        cv.connect(start, 0, set, 0);
        cv.connect(call, 0, set, 1);

        std::string ser = graph.serialize();
        BPGraph roundtrip;
        bool serOk = roundtrip.deserialize(ser) && roundtrip.serialize() == ser &&
                     roundtrip.funcs.size() == 1 && roundtrip.funcs[0].pure;
        BPInstance inst; inst.graph = &roundtrip; inst.entity = s.byId(owner); inst.initVars(nullptr);
        BPContext ctx; ctx.entity = inst.entity; ctx.scene = &s;
        inst.fire(BP_EV_START, ctx);
        float result = inst.vars["risultato"].single.asNum();
        bool ok = serOk && fabsf(result - 5.0f) < 1e-4f;
        snprintf(detail, sizeof(detail), "roundtrip=%s, SommaPura(2,3)=%g (5)", serOk ? "ok" : "NO", result);
        report("Blueprint v10 (funzioni pure)", ok, detail);
        if (!ok) failures++;
    }

    {
        // Blueprint v11: Get Component by Class accepts an Object target and
        // includes Audio Source among the serialized, backward-compatible classes.
        s.clear();
        int owner = s.spawnBox("component_owner", { 0, 1, 0 }, { 1, 1, 1 }, {}, BodyType::Static).id;
        int audioChild = s.spawnBox("audio_child", { 0, 2, 0 }, { 1, 1, 1 }, {}, BodyType::Static).id;
        Entity* child = s.byId(audioChild);
        child->parentId = owner;
        child->hasAudio = true;

        BPGraph graph;
        BPVarDef componentVar; componentVar.type = PIN_ENT; snprintf(componentVar.name, sizeof(componentVar.name), "component");
        BPVarDef foundVar; foundVar.type = PIN_BOOL; snprintf(foundVar.name, sizeof(foundVar.name), "found");
        graph.vars.push_back(componentVar); graph.vars.push_back(foundVar);
        BPCanvas& cv = graph.main();
        int start = cv.addNode(BP_EV_START, 0, 0);
        int get = cv.addNode(BP_GET_COMPONENT, 120, 0); cv.byId(get)->choice = 4; // Audio Source
        int setComponent = cv.addNode(BP_VAR_SET, 300, 0); snprintf(cv.byId(setComponent)->sname, 32, "component");
        int setFound = cv.addNode(BP_VAR_SET, 480, 0); snprintf(cv.byId(setFound)->sname, 32, "found");
        cv.connect(start, 0, setComponent, 0);
        cv.connect(get, 0, setComponent, 1);
        cv.connect(setComponent, 0, setFound, 0);
        cv.connect(get, 1, setFound, 1);

        std::string ser = graph.serialize();
        BPGraph roundtrip;
        bool serOk = roundtrip.deserialize(ser) && roundtrip.serialize() == ser;
        BPInstance inst; inst.graph = &roundtrip; inst.entity = s.byId(owner); inst.initVars(nullptr);
        BPContext ctx; ctx.entity = inst.entity; ctx.scene = &s;
        inst.fire(BP_EV_START, ctx);
        int result = inst.vars["component"].single.asEnt();
        bool found = inst.vars["found"].single.asBool();
        const BPNodeDef& getDef = bpDefs()[BP_GET_COMPONENT];
        bool nodeShape = getDef.nIns == 1 && getDef.ins[0].kind == PIN_ENT && BP_NCOMPS >= 7;
        int blueprintChild = s.spawnBox("blueprint_child", { 0, 3, 0 }, { 1, 1, 1 }, {}, BodyType::Static).id;
        Entity* scripted = s.byId(blueprintChild);
        scripted->parentId = owner;
        BlueprintComponentDef healthComponent;
        healthComponent.graphPath = "Blueprints\\Health.bp";
        scripted->additionalBlueprints.push_back(healthComponent);

        BPGraph exactGraph = graph;
        BPNode* exactGet = exactGraph.main().byId(get);
        exactGet->choice = 6;
        exactGet->slit[0] = "Blueprints\\Health.bp";
        std::string exactSer = exactGraph.serialize();
        BPGraph exactRoundtrip;
        bool exactSerOk = exactRoundtrip.deserialize(exactSer) &&
                          exactRoundtrip.main().byId(get) &&
                          exactRoundtrip.main().byId(get)->slit[0] == "Blueprints\\Health.bp";
        BPInstance exactInst; exactInst.graph = &exactRoundtrip; exactInst.entity = s.byId(owner); exactInst.initVars(nullptr);
        BPContext exactCtx; exactCtx.entity = exactInst.entity; exactCtx.scene = &s;
        exactInst.fire(BP_EV_START, exactCtx);
        int blueprintResult = exactInst.vars["component"].single.asEnt();
        bool blueprintFound = exactInst.vars["found"].single.asBool();

        exactRoundtrip.main().byId(get)->slit[0] = "Blueprints\\Missing.bp";
        BPInstance missingInst; missingInst.graph = &exactRoundtrip; missingInst.entity = s.byId(owner); missingInst.initVars(nullptr);
        BPContext missingCtx; missingCtx.entity = missingInst.entity; missingCtx.scene = &s;
        missingInst.fire(BP_EV_START, missingCtx);
        bool missingRejected = !missingInst.vars["found"].single.asBool() &&
                               missingInst.vars["component"].single.asEnt() == 0;

        bool ok = serOk && nodeShape && found && result == audioChild && exactSerOk &&
                  blueprintFound && blueprintResult == blueprintChild && missingRejected;
        snprintf(detail, sizeof(detail),
                 "native=%d/%d, Blueprint preciso=%d/%d, roundtrip=%s, missing=%s",
                 result, audioChild, blueprintResult, blueprintChild,
                 exactSerOk ? "ok" : "NO", missingRejected ? "ok" : "NO");
        report("Blueprint v11 (Get Component by Class)", ok, detail);
        if (!ok) failures++;
    }

    {
        // Blueprint v12: Audio Source actions use runtime callbacks so volume
        // changes are immediate and clip/fades can control the live voice.
        s.clear();
        int owner = s.spawnEmpty("audio_blueprint", {}).id;
        Entity* audio = s.byId(owner);
        audio->hasAudio = true;
        BPGraph graph;
        graph.interfaceAssets.push_back("Interfaces\\Interactable.bpi");
        BPVarDef implementsInterface;snprintf(implementsInterface.name,sizeof(implementsInterface.name),"ImplementsInteractable");implementsInterface.type=PIN_BOOL;graph.vars.push_back(implementsInterface);
        BPCanvas& cv = graph.main();
        int start = cv.addNode(BP_EV_START, 0, 0);
        int setClip = cv.addNode(BP_AUDIO_SET_CLIP, 120, 0);
        cv.byId(setClip)->slit[2] = "Audio\\Theme Loop.ogg";
        int setVolume = cv.addNode(BP_AUDIO_SET_VOLUME, 300, 0);
        cv.byId(setVolume)->lit[2].x = 1.65f;
        int fadeIn = cv.addNode(BP_AUDIO_FADE_IN, 480, 0);
        cv.byId(fadeIn)->lit[2].x = 0.75f;
        int fadeOut = cv.addNode(BP_AUDIO_FADE_OUT, 660, 0);
        cv.byId(fadeOut)->lit[2].x = 1.25f;
        int doesInterface=cv.addNode(BP_DOES_IMPLEMENT_INTERFACE,660,120);snprintf(cv.byId(doesInterface)->sname,sizeof(cv.byId(doesInterface)->sname),"Interfaces\\Interactable.bpi");
        int setImplements=cv.addNode(BP_VAR_SET,840,0);snprintf(cv.byId(setImplements)->sname,sizeof(cv.byId(setImplements)->sname),"ImplementsInteractable");
        cv.connect(start, 0, setClip, 0);
        cv.connect(setClip, 0, setVolume, 0);
        cv.connect(setVolume, 0, fadeIn, 0);
        cv.connect(fadeIn, 0, fadeOut, 0);
        cv.connect(fadeOut,0,setImplements,0);cv.connect(doesInterface,0,setImplements,1);

        std::string ser = graph.serialize();
        BPGraph roundtrip;
        bool serOk = roundtrip.deserialize(ser) && roundtrip.serialize() == ser &&
                     roundtrip.interfaceAssets.size() == 1 &&
                     roundtrip.interfaceAssets[0] == "Interfaces\\Interactable.bpi";
        bpAudioTestMask = 0;
        bpAudioTestVolume = bpAudioTestFadeIn = bpAudioTestFadeOut = 0;
        bpAudioTestClip.clear();
        BPInstance inst; inst.graph = &roundtrip; inst.entity = audio; inst.initVars(nullptr);
        BPContext ctx; ctx.entity = audio; ctx.scene = &s;
        ctx.setAudioClip = bpAudioTestSetClip;
        ctx.setAudioVolume = bpAudioTestSetVolume;
        ctx.fadeInAudio = bpAudioTestFadeInCb;
        ctx.fadeOutAudio = bpAudioTestFadeOutCb;
        inst.fire(BP_EV_START, ctx);
        bool interfaceNodeOk=inst.vars["ImplementsInteractable"].single.asBool()&&
                             bpDefs()[BP_DOES_IMPLEMENT_INTERFACE].ins[0].kind==PIN_ENT&&
                             bpDefs()[BP_DOES_IMPLEMENT_INTERFACE].outs[0].kind==PIN_BOOL;
        bool values = bpAudioTestMask == 15 && bpAudioTestClip == "Audio\\Theme Loop.ogg" && interfaceNodeOk&&
                      fabsf(bpAudioTestVolume - 1.65f) < 1e-4f &&
                      fabsf(bpAudioTestFadeIn - 0.75f) < 1e-4f &&
                      fabsf(bpAudioTestFadeOut - 1.25f) < 1e-4f;
        bool shape = bpDefs()[BP_AUDIO_SET_CLIP].ins[2].kind == PIN_STR &&
                     bpDefs()[BP_AUDIO_FADE_IN].nIns == 3 && bpDefs()[BP_AUDIO_FADE_OUT].nIns == 3;
        bool ok = serOk && values && shape;
        snprintf(detail, sizeof(detail), "roundtrip=%s, clip=%s, volume=%.2f, fade=%.2f/%.2f, interface=%s",
                 serOk ? "ok" : "NO", bpAudioTestClip.c_str(), bpAudioTestVolume,
                 bpAudioTestFadeIn, bpAudioTestFadeOut, interfaceNodeOk&&shape ? "ok" : "NO");
        report("Blueprint v12 (clip / volume live / fade / settings)", ok, detail);
        if (!ok) failures++;
    }

    {
        // Blueprint v13: delegate fan-out, Create Event, delegate reroute,
        // Clamp Float and Gate all retain stable serialized node ids.
        s.clear();
        int owner = s.spawnEmpty("bp_delegate_tools", {}).id;
        BPGraph graph;
        BPVarDef clamped; snprintf(clamped.name, sizeof(clamped.name), "clamped"); clamped.type = PIN_NUM;
        BPVarDef passed; snprintf(passed.name, sizeof(passed.name), "passed"); passed.type = PIN_BOOL;
        graph.vars.push_back(clamped);
        graph.vars.push_back(passed);
        BPEventDef eventDef; snprintf(eventDef.name, sizeof(eventDef.name), "Footstep"); graph.events.push_back(eventDef);
        BPCanvas& cv = graph.main();
        int custom = cv.addNode(BP_EV_CUSTOM, 0, 220); snprintf(cv.byId(custom)->sname, 32, "Footstep");
        int timerA = cv.addNode(BP_TIMER_SET, 250, 180);
        int timerB = cv.addNode(BP_TIMER_SET, 250, 300);
        cv.connect(custom, 1, timerA, 3);
        cv.connect(custom, 1, timerB, 3);
        int customFanout = 0;
        for (const BPLink& link : cv.links) if (link.fromNode == custom && link.fromPin == 1) customFanout++;

        int create = cv.addNode(BP_CREATE_EVENT, 0, 430); snprintf(cv.byId(create)->sname, 32, "Footstep");
        int timerC = cv.addNode(BP_TIMER_SET, 250, 420);
        int timerD = cv.addNode(BP_TIMER_SET, 250, 540);
        cv.connect(create, 0, timerC, 3);
        cv.connect(create, 0, timerD, 3);
        int createFanout = 0;
        for (const BPLink& link : cv.links) if (link.fromNode == create && link.fromPin == 0) createFanout++;
        int reroute = cv.addNode(BP_REROUTE, 150, 650);
        int timerE = cv.addNode(BP_TIMER_SET, 300, 650);
        cv.connect(custom, 1, reroute, 0);
        cv.connect(reroute, 0, timerE, 3);

        int start = cv.addNode(BP_EV_START, 0, 0);
        int sequence = cv.addNode(BP_FLOW_SEQ, 100, 0);
        int gate = cv.addNode(BP_FLOW_GATE, 250, 0);
        int setPassed = cv.addNode(BP_VAR_SET, 430, 0); snprintf(cv.byId(setPassed)->sname, 32, "passed");
        cv.byId(setPassed)->lit[1].x = 1;
        int clamp = cv.addNode(BP_M_CLAMP_FLOAT, 260, 100);
        cv.byId(clamp)->lit[0].x = 2.5f;
        cv.byId(clamp)->lit[1].x = 0.2f;
        cv.byId(clamp)->lit[2].x = 0.8f;
        int setClamp = cv.addNode(BP_VAR_SET, 430, 110); snprintf(cv.byId(setClamp)->sname, 32, "clamped");
        cv.connect(start, 0, sequence, 0);
        cv.connect(sequence, 0, gate, 1); // Open
        cv.connect(sequence, 1, gate, 0); // Enter
        cv.connect(gate, 0, setPassed, 0);
        cv.connect(sequence, 2, setClamp, 0);
        cv.connect(clamp, 0, setClamp, 1);

        std::string ser = graph.serialize();
        BPGraph roundtrip;
        bool serOk = roundtrip.deserialize(ser) && roundtrip.serialize() == ser;
        BPInstance inst; inst.graph = &roundtrip; inst.entity = s.byId(owner); inst.initVars(nullptr);
        BPContext ctx; ctx.entity = inst.entity; ctx.scene = &s;
        inst.fire(BP_EV_START, ctx);
        float clampResult = inst.vars["clamped"].single.asNum();
        bool gatePassed = inst.vars["passed"].single.asBool();
        bool shapes = bpDefs()[BP_CREATE_EVENT].outs[0].kind == PIN_DELEGATE &&
                      bpDefs()[BP_M_CLAMP_FLOAT].nIns == 3 && bpDefs()[BP_FLOW_GATE].nIns >= 3 &&
                      bpDefs()[BP_REROUTE].outs[0].kind == PIN_ANY;
        bool ok = serOk && customFanout == 2 && createFanout == 2 && shapes && gatePassed &&
                  fabsf(clampResult - 0.8f) < 1e-4f;
        snprintf(detail, sizeof(detail), "roundtrip=%s, delegate fanout=%d/%d, reroute=%s, gate=%s, clamp=%.2f",
                 serOk ? "ok" : "NO", customFanout, createFanout,
                 shapes ? "ok" : "NO", gatePassed ? "ok" : "NO", clampResult);
        report("Blueprint v13 (delegate fan-out / Create Event / Clamp / Gate / reroute)", ok, detail);
        if (!ok) failures++;
    }

    {
        // Blueprint v14: inclusive comparison operators remain serialized and
        // evaluate correctly at the equality boundary.
        s.clear();
        int owner = s.spawnEmpty("bp_compare_inclusive", {}).id;
        BPGraph graph;
        BPVarDef le; snprintf(le.name, sizeof(le.name), "less_equal"); le.type = PIN_BOOL;
        BPVarDef ge; snprintf(ge.name, sizeof(ge.name), "greater_equal"); ge.type = PIN_BOOL;
        graph.vars.push_back(le);
        graph.vars.push_back(ge);
        BPCanvas& cv = graph.main();
        int start = cv.addNode(BP_EV_START, 0, 0);
        int seq = cv.addNode(BP_FLOW_SEQ, 100, 0);
        int cmpLE = cv.addNode(BP_L_CMP, 220, 0);
        cv.byId(cmpLE)->choice = 3;
        cv.byId(cmpLE)->lit[0].x = 2.0f;
        cv.byId(cmpLE)->lit[1].x = 2.0f;
        int setLE = cv.addNode(BP_VAR_SET, 390, 0); snprintf(cv.byId(setLE)->sname, 32, "less_equal");
        int cmpGE = cv.addNode(BP_L_CMP, 220, 120);
        cv.byId(cmpGE)->choice = 4;
        cv.byId(cmpGE)->lit[0].x = 3.0f;
        cv.byId(cmpGE)->lit[1].x = 2.0f;
        int setGE = cv.addNode(BP_VAR_SET, 390, 120); snprintf(cv.byId(setGE)->sname, 32, "greater_equal");
        cv.connect(start, 0, seq, 0);
        cv.connect(seq, 0, setLE, 0);
        cv.connect(cmpLE, 0, setLE, 1);
        cv.connect(seq, 1, setGE, 0);
        cv.connect(cmpGE, 0, setGE, 1);

        std::string ser = graph.serialize();
        BPGraph roundtrip;
        bool serOk = roundtrip.deserialize(ser) && roundtrip.serialize() == ser;
        BPInstance inst; inst.graph = &roundtrip; inst.entity = s.byId(owner); inst.initVars(nullptr);
        BPContext ctx; ctx.entity = inst.entity; ctx.scene = &s;
        inst.fire(BP_EV_START, ctx);
        bool leResult = inst.vars["less_equal"].single.asBool();
        bool geResult = inst.vars["greater_equal"].single.asBool();
        bool labels = strcmp(BP_CMP_OPS[3], "<=") == 0 && strcmp(BP_CMP_OPS[4], ">=") == 0;
        bool ok = serOk && leResult && geResult && labels;
        snprintf(detail, sizeof(detail), "roundtrip=%s, 2<=2=%s, 3>=2=%s, labels=%s",
                 serOk ? "ok" : "NO", leResult ? "true" : "false",
                 geResult ? "true" : "false", labels ? "ok" : "NO");
        report("Blueprint v14 (Compare <= / >=)", ok, detail);
        if (!ok) failures++;
    }

    {
        // Blueprint v15: deleting reroutes preserves both data and exec paths;
        // Ctrl-style endpoint detaching can reconnect the same wire elsewhere.
        BPGraph graph;
        BPVarDef a; snprintf(a.name, sizeof(a.name), "a"); a.type = PIN_NUM;
        BPVarDef b; snprintf(b.name, sizeof(b.name), "b"); b.type = PIN_NUM;
        graph.vars.push_back(a); graph.vars.push_back(b);
        BPCanvas& cv = graph.main();
        int start = cv.addNode(BP_EV_START, 0, 0);
        int execReroute = cv.addNode(BP_REROUTE_EX, 100, 0);
        int value = cv.addNode(BP_VAL_NUM, 0, 100);
        int dataReroute = cv.addNode(BP_REROUTE, 100, 100);
        int setA = cv.addNode(BP_VAR_SET, 220, 50); snprintf(cv.byId(setA)->sname, 32, "a");
        int setB = cv.addNode(BP_VAR_SET, 420, 50); snprintf(cv.byId(setB)->sname, 32, "b");
        cv.connect(start, 0, execReroute, 0);
        cv.connect(execReroute, 0, setA, 0);
        cv.connect(value, 0, dataReroute, 0);
        cv.connect(dataReroute, 0, setA, 1);
        cv.removeNode(execReroute);
        cv.removeNode(dataReroute);
        const BPLink* execBypass = cv.linkInto(setA, 0);
        const BPLink* dataBypass = cv.linkInto(setA, 1);
        bool bypassOk = execBypass && execBypass->fromNode == start &&
                        dataBypass && dataBypass->fromNode == value;
        BPLink moved;
        bool detached = cv.detachLinkAtPin(setA, 1, false, moved);
        if (detached) cv.connect(moved.fromNode, moved.fromPin, setB, 1);
        bool rewireOk = !cv.linkInto(setA, 1) && cv.linkInto(setB, 1) &&
                        cv.linkInto(setB, 1)->fromNode == value;
        std::string ser = graph.serialize();
        BPGraph roundtrip;
        bool serOk = roundtrip.deserialize(ser) && roundtrip.serialize() == ser;
        bool ok = bypassOk && detached && rewireOk && serOk;
        snprintf(detail, sizeof(detail), "bypass exec/data=%s, detach=%s, reconnect=%s, roundtrip=%s",
                 bypassOk ? "ok" : "NO", detached ? "ok" : "NO",
                 rewireOk ? "ok" : "NO", serOk ? "ok" : "NO");
        report("Blueprint v15 (Ctrl rewire / reroute bypass)", ok, detail);
        if (!ok) failures++;
    }

    {
        // Blueprint v16: Pi is a pure Float constant and survives graph
        // serialization without shifting any of the older node identifiers.
        s.clear();
        int owner = s.spawnEmpty("bp_pi", {}).id;
        BPGraph graph;
        BPVarDef result; snprintf(result.name, sizeof(result.name), "pi_result"); result.type = PIN_NUM;
        graph.vars.push_back(result);
        BPCanvas& cv = graph.main();
        int start = cv.addNode(BP_EV_START, 0, 0);
        int pi = cv.addNode(BP_M_PI, 120, 80);
        int set = cv.addNode(BP_VAR_SET, 280, 0); snprintf(cv.byId(set)->sname, 32, "pi_result");
        cv.connect(start, 0, set, 0);
        cv.connect(pi, 0, set, 1);
        std::string ser = graph.serialize();
        BPGraph roundtrip;
        bool serOk = roundtrip.deserialize(ser) && roundtrip.serialize() == ser;
        BPInstance inst; inst.graph = &roundtrip; inst.entity = s.byId(owner); inst.initVars(nullptr);
        BPContext ctx; ctx.entity = inst.entity; ctx.scene = &s;
        inst.fire(BP_EV_START, ctx);
        float value = inst.vars["pi_result"].single.asNum();
        const BPNodeDef& def = bpDefs()[BP_M_PI];
        bool shapeOk = def.category == 4 && def.nIns == 0 && def.nOuts == 1 && def.outs[0].kind == PIN_NUM;
        bool ok = serOk && shapeOk && fabsf(value - 3.14159265f) < 1e-6f;
        snprintf(detail, sizeof(detail), "roundtrip=%s, shape=%s, value=%.8f",
                 serOk ? "ok" : "NO", shapeOk ? "ok" : "NO", value);
        report("Blueprint v16 (Pi constant)", ok, detail);
        if (!ok) failures++;
    }

    {
        // Blueprint v17: enum values, Select/Switch, Spawn result, Exposed on
        // Spawn metadata, node strings with spaces and Construction Script.
        s.clear();
        int owner = s.spawnEmpty("bp_spawn_enum", {}).id;
        BPEnumAsset enumAsset;
        enumAsset.values = { "Idle", "Walk", "Fast Run" };
        BPEnumAsset enumRoundtrip;
        bool enumAssetOk = enumRoundtrip.deserialize(enumAsset.serialize()) &&
                           enumRoundtrip.values == enumAsset.values;

        BPGraph graph;
        BPVarDef selected; snprintf(selected.name, sizeof(selected.name), "selected"); selected.type = PIN_NUM;
        BPVarDef switched; snprintf(switched.name, sizeof(switched.name), "switched"); switched.type = PIN_BOOL;
        BPVarDef spawned; snprintf(spawned.name, sizeof(spawned.name), "spawned"); spawned.type = PIN_ENT;
        BPVarDef exposed; snprintf(exposed.name, sizeof(exposed.name), "speed"); exposed.type = PIN_NUM;
        exposed.scope = VS_PUBLIC; exposed.exposeOnSpawn = true;
        graph.vars.push_back(selected); graph.vars.push_back(switched); graph.vars.push_back(spawned); graph.vars.push_back(exposed);
        BPCanvas& cv = graph.main();
        int start = cv.addNode(BP_EV_START, 0, 0);
        int seq = cv.addNode(BP_FLOW_SEQ, 100, 0);
        int select = cv.addNode(BP_SELECT_ENUM, 220, 0);
        cv.byId(select)->lit[0].x = 1;
        cv.byId(select)->lit[1].x = 3;
        cv.byId(select)->lit[2].x = 7;
        int setSelected = cv.addNode(BP_VAR_SET, 430, 0); snprintf(cv.byId(setSelected)->sname, 32, "selected");
        int sw = cv.addNode(BP_SWITCH_ENUM, 220, 100);
        cv.byId(sw)->lit[1].x = 1;
        int setSwitched = cv.addNode(BP_VAR_SET, 430, 100); snprintf(cv.byId(setSwitched)->sname, 32, "switched");
        cv.byId(setSwitched)->lit[1].x = 1;
        int spawn = cv.addNode(BP_SPAWN_PREFAB, 220, 210);
        snprintf(cv.byId(spawn)->sname, sizeof(cv.byId(spawn)->sname), "Prefabs\\Enemy Runner.pfb");
        int setSpawned = cv.addNode(BP_VAR_SET, 470, 210); snprintf(cv.byId(setSpawned)->sname, 32, "spawned");
        cv.connect(start, 0, seq, 0);
        cv.connect(seq, 0, setSelected, 0); cv.connect(select, 0, setSelected, 1);
        cv.connect(seq, 1, sw, 0); cv.connect(sw, 1, setSwitched, 0);
        cv.connect(seq, 2, spawn, 0); cv.connect(spawn, 0, setSpawned, 0); cv.connect(spawn, 1, setSpawned, 1);

        std::string serialized = graph.serialize();
        BPGraph roundtrip;
        bool serializationOk = roundtrip.deserialize(serialized) && roundtrip.serialize() == serialized &&
                               roundtrip.vars.size() == 4 && roundtrip.vars[3].exposeOnSpawn;
        BPNode* spawnAfter = nullptr;
        for (BPNode& node : roundtrip.main().nodes) if (node.def == BP_SPAWN_PREFAB) spawnAfter = &node;
        bool pathOk = spawnAfter && strcmp(spawnAfter->sname, "Prefabs\\Enemy Runner.pfb") == 0;
        bool constructionOk = false;
        for (const BPFunc& canvas : roundtrip.graphs)
            if (strcmp(canvas.name, "ConstructionScript") == 0 && !canvas.body.nodes.empty() &&
                canvas.body.nodes[0].def == BP_EV_CONSTRUCT) constructionOk = true;

        BPInstance instance; instance.graph = &roundtrip; instance.entity = s.byId(owner); instance.initVars(nullptr);
        BPContext context; context.entity = instance.entity; context.scene = &s; context.spawnPrefab = bpSpawnTestCb;
        bpSpawnTestCalls = 0;
        instance.fire(BP_EV_START, context);
        bool runtimeOk = fabsf(instance.vars["selected"].single.asNum() - 7.0f) < 1e-4f &&
                         instance.vars["switched"].single.asBool() &&
                         instance.vars["spawned"].single.asEnt() == 77 && bpSpawnTestCalls == 1;
        bool shapesOk = bpDefs()[BP_SPAWN_PREFAB].outs[1].kind == PIN_ENT &&
                        bpDefs()[BP_SELECT_ENUM].ins[0].kind == PIN_ENUM &&
                        bpDefs()[BP_SWITCH_ENUM].ins[1].kind == PIN_ENUM;
        bool ok = enumAssetOk && serializationOk && pathOk && constructionOk && runtimeOk && shapesOk;
        snprintf(detail, sizeof(detail), "asset=%s, roundtrip/path=%s, construct=%s, select/switch/spawn=%s",
                 enumAssetOk ? "ok" : "NO", serializationOk && pathOk ? "ok" : "NO",
                 constructionOk ? "ok" : "NO", runtimeOk && shapesOk ? "ok" : "NO");
        report("Blueprint v17 (Spawn / Exposed / Enum / Construction)", ok, detail);
        if (!ok) failures++;
    }

    {
        // Blueprint v18: decoupled actor lookup by tag and class/component loops.
        s.clear();
        int ownerId = s.spawnEmpty("lookup_owner", {}).id;
        int playerId = s.spawnEmpty("player", {}).id;
        s.byId(playerId)->tags.push_back("Player");
        snprintf(s.byId(playerId)->graphPath, sizeof(s.byId(playerId)->graphPath), "Characters\\Player.bp");
        int companionId = s.spawnEmpty("companion", {}).id;
        s.byId(companionId)->tags.push_back("Player");
        s.byId(companionId)->hasAIAgent = true;
        int enemyId = s.spawnEmpty("enemy", {}).id;
        s.byId(enemyId)->hasAIAgent = true;
        snprintf(s.byId(enemyId)->graphPath, sizeof(s.byId(enemyId)->graphPath), "Characters\\Enemy.bp");

        std::string sceneSerialized = s.serialize();
        EditorScene sceneRoundtrip;
        bool tagsRoundtrip = sceneRoundtrip.deserialize(sceneSerialized) &&
                             sceneRoundtrip.byId(playerId) && sceneRoundtrip.byId(playerId)->tags.size() == 1 &&
                             sceneRoundtrip.byId(playerId)->tags[0] == "Player";

        BPGraph graph;
        auto addActorVar = [&](const char* name, VarContainer container) {
            BPVarDef v;
            snprintf(v.name, sizeof(v.name), "%s", name);
            v.type = PIN_ENT;
            v.container = container;
            graph.vars.push_back(v);
        };
        addActorVar("firstPlayer", VC_SINGLE);
        addActorVar("taggedPlayers", VC_ARRAY);
        addActorVar("agents", VC_ARRAY);
        addActorVar("enemyClass", VC_ARRAY);

        BPCanvas& cv = graph.main();
        int start = cv.addNode(BP_EV_START, 0, 0);
        int find = cv.addNode(BP_FIND_BY_TAG, 100, 0);
        cv.byId(find)->slit[0] = " player- "; // canonical matching ignores case/separators/outer spaces
        int setFirst = cv.addNode(BP_VAR_SET, 260, 0);
        snprintf(cv.byId(setFirst)->sname, sizeof(cv.byId(setFirst)->sname), "firstPlayer");
        int byTag = cv.addNode(BP_GET_ALL_WITH_TAG, 430, 0);
        cv.byId(byTag)->slit[1] = "Player";
        int addTag = cv.addNode(BP_ARR_ADD, 650, 0);
        snprintf(cv.byId(addTag)->sname, sizeof(cv.byId(addTag)->sname), "taggedPlayers");
        int byComponent = cv.addNode(BP_GET_ALL_WITH_CLASS, 430, 150);
        snprintf(cv.byId(byComponent)->sname, sizeof(cv.byId(byComponent)->sname), "component:8");
        int addAgent = cv.addNode(BP_ARR_ADD, 650, 150);
        snprintf(cv.byId(addAgent)->sname, sizeof(cv.byId(addAgent)->sname), "agents");
        int byBlueprint = cv.addNode(BP_GET_ALL_WITH_CLASS, 430, 300);
        snprintf(cv.byId(byBlueprint)->sname, sizeof(cv.byId(byBlueprint)->sname), "blueprint:Characters\\Enemy.bp");
        int addEnemy = cv.addNode(BP_ARR_ADD, 650, 300);
        snprintf(cv.byId(addEnemy)->sname, sizeof(cv.byId(addEnemy)->sname), "enemyClass");

        cv.connect(start, 0, setFirst, 0);
        cv.connect(find, 0, setFirst, 1);
        cv.connect(setFirst, 0, byTag, 0);
        cv.connect(byTag, 0, addTag, 0); cv.connect(byTag, 1, addTag, 1);
        cv.connect(byTag, 3, byComponent, 0);
        cv.connect(byComponent, 0, addAgent, 0); cv.connect(byComponent, 1, addAgent, 1);
        cv.connect(byComponent, 3, byBlueprint, 0);
        cv.connect(byBlueprint, 0, addEnemy, 0); cv.connect(byBlueprint, 1, addEnemy, 1);

        std::string graphSerialized = graph.serialize();
        BPGraph graphRoundtrip;
        bool graphOk = graphRoundtrip.deserialize(graphSerialized) && graphRoundtrip.serialize() == graphSerialized;
        BPInstance instance; instance.graph = &graphRoundtrip; instance.entity = s.byId(ownerId); instance.initVars(nullptr);
        BPContext context; context.entity = instance.entity; context.scene = &s;
        instance.fire(BP_EV_START, context);
        bool runtimeOk = instance.vars["firstPlayer"].single.asEnt() == playerId &&
                         instance.vars["taggedPlayers"].arr.size() == 2 &&
                         instance.vars["agents"].arr.size() == 2 &&
                         instance.vars["enemyClass"].arr.size() == 1 &&
                         instance.vars["enemyClass"].arr[0].asEnt() == enemyId;
        bool shapesOk = bpDefs()[BP_FIND_BY_TAG].outs[0].kind == PIN_ENT &&
                        bpDefs()[BP_FIND_BY_TAG].outs[1].kind == PIN_BOOL &&
                        bpDefs()[BP_GET_ALL_WITH_CLASS].outs[3].kind == PIN_EXEC &&
                        bpDefs()[BP_GET_ALL_WITH_TAG].ins[1].kind == PIN_STR;
        bool ok = tagsRoundtrip && graphOk && runtimeOk && shapesOk;
        snprintf(detail, sizeof(detail), "tag scene=%s, graph=%s, first/all tag=%s, class/component=%s",
                 tagsRoundtrip ? "ok" : "NO", graphOk ? "ok" : "NO",
                 runtimeOk ? "ok" : "NO", runtimeOk && shapesOk ? "ok" : "NO");
        report("Blueprint v18 (Actor lookup by Tag / Class)", ok, detail);
        if (!ok) failures++;
    }

    {
        // Blueprint v19: parent/child inheritance, framework metadata and getters.
        fs::path tempRoot = fs::path(g.baseDir) / "build" / "test_bp_inheritance";
        std::error_code tempEc;
        fs::create_directories(tempRoot, tempEc);
        BPGraph parent;
        parent.classKind = BP_CLASS_PLAYERCONTROLLER;
        parent.defaultTags.push_back("Controllable");
        BPVarDef inherited; snprintf(inherited.name, sizeof(inherited.name), "inherited"); inherited.type = PIN_NUM;
        parent.vars.push_back(inherited);
        int parentStart = parent.main().addNode(BP_EV_START, 0, 0);
        int parentSet = parent.main().addNode(BP_VAR_SET, 100, 0);
        snprintf(parent.main().byId(parentSet)->sname, sizeof(parent.main().byId(parentSet)->sname), "inherited");
        parent.main().byId(parentSet)->lit[1].x = 11;
        parent.main().connect(parentStart, 0, parentSet, 0);

        BPGraph child;
        child.classKind = BP_CLASS_PLAYERCONTROLLER;
        child.parentAsset = "Parent.bp";
        child.defaultTags.push_back("Child");
        BPVarDef local; snprintf(local.name, sizeof(local.name), "local"); local.type = PIN_ENT;
        child.vars.push_back(local);
        int childStart = child.main().addNode(BP_EV_START, 0, 0);
        int childSet = child.main().addNode(BP_VAR_SET, 100, 0);
        snprintf(child.main().byId(childSet)->sname, sizeof(child.main().byId(childSet)->sname), "local");
        int getPawn = child.main().addNode(BP_GET_PLAYER_PAWN, 0, 100);
        child.main().connect(childStart, 0, childSet, 0);
        child.main().connect(getPawn, 0, childSet, 1);

        bool filesOk = writeFile((tempRoot / "Parent.bp").string(), parent.serialize()) &&
                       writeFile((tempRoot / "Child.bp").string(), child.serialize());
        BPGraph resolved;
        bool inheritOk = filesOk && bpLoadResolvedGraph(tempRoot.string(), "Child.bp", resolved) &&
                         resolved.findVar("inherited") && resolved.findVar("local") && resolved.graphs.size() >= 4 &&
                         resolved.classKind == BP_CLASS_PLAYERCONTROLLER && resolved.defaultTags.size() == 2;
        Entity frameworkOwner;
        frameworkOwner.id = 99;
        BPInstance instance; instance.graph = &resolved; instance.entity = &frameworkOwner; instance.initVars(nullptr);
        BPContext context; context.entity = &frameworkOwner; context.playerPawnEntity = 77;
        instance.fire(BP_EV_START, context);
        bool runtimeOk = fabsf(instance.vars["inherited"].single.asNum() - 11) < 1e-4f &&
                         instance.vars["local"].single.asEnt() == 77;
        std::string savedProjectDir = g.projectDir;
        g.projectDir = tempRoot.string();
        g.bpScripts.clear();
        App::LiveScript saveObject; saveObject.entityId = 99; saveObject.inst = instance;
        saveObject.inst.vars["inherited"].single = BPValue::N(42);
        g.bpScripts.push_back(std::move(saveObject));
        bool saveOk = bpSaveGameSlotCb(99, "Unit Slot") && bpSaveGameExistsCb("Unit Slot");
        g.bpScripts[0].inst.vars["inherited"].single = BPValue::N(0);
        bool loadOk = bpLoadGameSlotCb(99, "Unit Slot") &&
                      fabsf(g.bpScripts[0].inst.vars["inherited"].single.asNum() - 42) < 1e-4f;
        fs::remove(saveGameSlotPath("Unit Slot"), tempEc);
        fs::remove(tempRoot / "Saved" / "SaveGames", tempEc);
        fs::remove(tempRoot / "Saved", tempEc);
        g.bpScripts.clear();
        g.projectDir = savedProjectDir;
        EditorScene frameworkScene;
        frameworkScene.gameModePath = "Framework\\MainGameMode.bp";
        std::string sceneData = frameworkScene.serialize();
        EditorScene frameworkScene2;
        bool sceneOk = frameworkScene2.deserialize(sceneData) && frameworkScene2.gameModePath == frameworkScene.gameModePath;
        fs::remove(tempRoot / "Parent.bp", tempEc);
        fs::remove(tempRoot / "Child.bp", tempEc);
        fs::remove(tempRoot, tempEc);
        bool shapesOk = bpDefs()[BP_GET_GAME_MODE].outs[0].kind == PIN_ENT &&
                        bpDefs()[BP_GET_GAME_INSTANCE].outs[0].kind == PIN_ENT &&
                        bpDefs()[BP_GET_PLAYER_CONTROLLER].outs[0].kind == PIN_ENT &&
                        bpDefs()[BP_GET_PLAYER_PAWN].outs[0].kind == PIN_ENT &&
                        bpDefs()[BP_SAVE_GAME_SLOT].outs[1].kind == PIN_BOOL &&
                        bpDefs()[BP_CREATE_SAVE_GAME].outs[1].kind == PIN_ENT;
        bool ok = inheritOk && runtimeOk && sceneOk && shapesOk && saveOk && loadOk;
        snprintf(detail, sizeof(detail), "parent=%s, runtime=%s, framework=%s, save/load=%s",
                 inheritOk ? "ok" : "NO", runtimeOk ? "ok" : "NO", sceneOk && shapesOk ? "ok" : "NO",
                 saveOk && loadOk ? "ok" : "NO");
        report("Blueprint v19 (Framework / Child inheritance)", ok, detail);
        if (!ok) failures++;
    }

    {
        // Interface Message: signature dinamica dalla .bpi, output di ritorno e
        // no-op sicuro quando il Target non implementa l'interfaccia.
        fs::path tempRoot=fs::path(g.baseDir)/"build"/"test_bp_interface_message";std::error_code ec;fs::create_directories(tempRoot,ec);
        BPGraph interfaceAsset;BPFunc signature;snprintf(signature.name,sizeof(signature.name),"Interact");signature.pure=true;
        BPFuncPin amount;snprintf(amount.name,sizeof(amount.name),"Amount");amount.kind=PIN_NUM;signature.ins.push_back(amount);
        BPFuncPin resultPin;snprintf(resultPin.name,sizeof(resultPin.name),"Result");resultPin.kind=PIN_NUM;signature.outs.push_back(resultPin);interfaceAsset.funcs.push_back(signature);
        bool interfaceFileOk=writeFile((tempRoot/"Interactable.bpi").string(),interfaceAsset.serialize());

        BPGraph implementer;implementer.interfaceAssets.push_back("Interactable.bpi");BPFunc implementation=signature;
        int entry=implementation.body.addNode(BP_FN_ENTRY,0,0),ret=implementation.body.addNode(BP_FN_RETURN,180,0);
        implementation.body.connect(entry,0,ret,0);implementer.funcs.push_back(std::move(implementation));
        BPGraph incompatible;
        s.clear();int targetId=s.spawnEmpty("InterfaceTarget",{}).id;int incompatibleId=s.spawnEmpty("PlainTarget",{}).id;int callerId=s.spawnEmpty("Caller",{}).id;
        BPGraph caller;BPVarDef answered;snprintf(answered.name,sizeof(answered.name),"Answered");answered.type=PIN_NUM;caller.vars.push_back(answered);
        BPVarDef ignored;snprintf(ignored.name,sizeof(ignored.name),"Ignored");ignored.type=PIN_NUM;caller.vars.push_back(ignored);
        BPCanvas& messageCanvas=caller.main();int start=messageCanvas.addNode(BP_EV_START,0,0);
        int message=messageCanvas.addNode(BP_INTERFACE_MESSAGE,120,0);messageCanvas.byId(message)->slit[0]="Interactable.bpi";snprintf(messageCanvas.byId(message)->sname,sizeof(messageCanvas.byId(message)->sname),"Interact");messageCanvas.byId(message)->lit[1].x=(float)targetId;messageCanvas.byId(message)->lit[2].x=7;
        int setAnswered=messageCanvas.addNode(BP_VAR_SET,340,0);snprintf(messageCanvas.byId(setAnswered)->sname,sizeof(messageCanvas.byId(setAnswered)->sname),"Answered");
        int missingMessage=messageCanvas.addNode(BP_INTERFACE_MESSAGE,520,0);
        messageCanvas.byId(missingMessage)->slit[0]="Interactable.bpi";snprintf(messageCanvas.byId(missingMessage)->sname,sizeof(messageCanvas.byId(missingMessage)->sname),"Interact");messageCanvas.byId(missingMessage)->lit[1].x=(float)incompatibleId;messageCanvas.byId(missingMessage)->lit[2].x=99;
        int setIgnored=messageCanvas.addNode(BP_VAR_SET,740,0);snprintf(messageCanvas.byId(setIgnored)->sname,sizeof(messageCanvas.byId(setIgnored)->sname),"Ignored");
        messageCanvas.connect(start,0,message,0);messageCanvas.connect(message,0,setAnswered,0);messageCanvas.connect(message,1,setAnswered,1);
        messageCanvas.connect(setAnswered,0,missingMessage,0);messageCanvas.connect(missingMessage,0,setIgnored,0);messageCanvas.connect(missingMessage,1,setIgnored,1);
        BPGraph callerRoundtrip;bool graphOk=callerRoundtrip.deserialize(caller.serialize())&&callerRoundtrip.main().byId(message)&&callerRoundtrip.main().byId(message)->slit[0]=="Interactable.bpi";
        std::string savedBPProject=gBPProjectDir,savedProject=g.projectDir;gBPProjectDir=tempRoot.string();g.projectDir=tempRoot.string();g.bpScripts.clear();
        App::LiveScript targetLive;targetLive.entityId=targetId;targetLive.inst.graph=&implementer;targetLive.inst.entity=s.byId(targetId);targetLive.inst.initVars(nullptr);g.bpScripts.push_back(std::move(targetLive));
        App::LiveScript incompatibleLive;incompatibleLive.entityId=incompatibleId;incompatibleLive.inst.graph=&incompatible;incompatibleLive.inst.entity=s.byId(incompatibleId);incompatibleLive.inst.initVars(nullptr);g.bpScripts.push_back(std::move(incompatibleLive));
        BPInstance callerInstance;callerInstance.graph=&callerRoundtrip;callerInstance.entity=s.byId(callerId);callerInstance.initVars(nullptr);BPContext messageContext;messageContext.entity=callerInstance.entity;messageContext.scene=&s;messageContext.callInterfaceMessage=bpCallInterfaceMessageCb;callerInstance.fire(BP_EV_START,messageContext);
        bool runtimeOk=fabsf(callerInstance.vars["Answered"].single.asNum()-7)<.001f&&fabsf(callerInstance.vars["Ignored"].single.asNum())<.001f;
        bool shapeOk=bpDefs()[BP_INTERFACE_MESSAGE].ins[1].kind==PIN_ENT&&bpDefs()[BP_INTERFACE_MESSAGE].outs[0].kind==PIN_EXEC;
        g.bpScripts.clear();gBPProjectDir=savedBPProject;g.projectDir=savedProject;fs::remove(tempRoot/"Interactable.bpi",ec);fs::remove(tempRoot,ec);
        bool ok=interfaceFileOk&&graphOk&&runtimeOk&&shapeOk;snprintf(detail,sizeof(detail),"firma/roundtrip=%s, target=%s, target incompatibile=no-op %s",interfaceFileOk&&graphOk?"ok":"NO",runtimeOk?"ok":"NO",runtimeOk?"ok":"NO");
        report("Blueprint Interface Message",ok,detail);if(!ok)failures++;
    }

    {
        // Persistent Inspector Events: multiple listeners, typed arguments,
        // Custom Event outputs and scene round-trip.
        s.clear();g.bpScripts.clear();
        int ownerId=s.spawnEmpty("Button",{}).id;int receiverId=s.spawnEmpty("Receiver",{}).id;
        Entity* owner=s.byId(ownerId);owner->hasInspectorEvents=true;Entity* receiverEntity=s.byId(receiverId);receiverEntity->hasAudio=true;
        BPGraph receiverGraph;BPVarDef functionValue;snprintf(functionValue.name,sizeof(functionValue.name),"FunctionValue");receiverGraph.vars.push_back(functionValue);
        BPVarDef eventValue;snprintf(eventValue.name,sizeof(eventValue.name),"EventValue");receiverGraph.vars.push_back(eventValue);
        BPFunc function;snprintf(function.name,sizeof(function.name),"SetAmount");function.ins.clear();function.outs.clear();
        BPFuncPin amount;snprintf(amount.name,sizeof(amount.name),"Amount");amount.kind=PIN_NUM;function.ins.push_back(amount);
        int functionEntry=function.body.addNode(BP_FN_ENTRY,0,0),functionSet=function.body.addNode(BP_VAR_SET,180,0);
        snprintf(function.body.byId(functionSet)->sname,sizeof(function.body.byId(functionSet)->sname),"FunctionValue");
        function.body.connect(functionEntry,0,functionSet,0);function.body.connect(functionEntry,1,functionSet,1);receiverGraph.funcs.push_back(std::move(function));
        BPEventDef eventSignature;snprintf(eventSignature.name,sizeof(eventSignature.name),"OnInspector");eventSignature.params.push_back(amount);receiverGraph.events.push_back(eventSignature);
        int custom=receiverGraph.main().addNode(BP_EV_CUSTOM,0,0),eventSet=receiverGraph.main().addNode(BP_VAR_SET,180,0);
        snprintf(receiverGraph.main().byId(custom)->sname,sizeof(receiverGraph.main().byId(custom)->sname),"OnInspector");
        snprintf(receiverGraph.main().byId(eventSet)->sname,sizeof(receiverGraph.main().byId(eventSet)->sname),"EventValue");
        receiverGraph.main().connect(custom,0,eventSet,0);receiverGraph.main().connect(custom,1,eventSet,1);
        App::LiveScript receiverLive;receiverLive.entityId=receiverId;receiverLive.inst.graph=&receiverGraph;receiverLive.inst.entity=s.byId(receiverId);receiverLive.inst.initVars(nullptr);g.bpScripts.push_back(std::move(receiverLive));
        InspectorEventDef persistent;persistent.name="OnPressed";
        InspectorEventListener functionListener;functionListener.targetEntity=receiverId;functionListener.callable="SetAmount";
        InspectorEventArgument functionArg;functionArg.kind=PIN_NUM;functionArg.value.x=12;persistent.listeners.push_back(functionListener);persistent.listeners.back().arguments.push_back(functionArg);
        InspectorEventListener customListener;customListener.targetEntity=receiverId;customListener.customEvent=true;customListener.callable="OnInspector";
        InspectorEventArgument customArg;customArg.kind=PIN_NUM;customArg.value.x=34;persistent.listeners.push_back(customListener);persistent.listeners.back().arguments.push_back(customArg);
        InspectorEventListener nativeListener;nativeListener.targetEntity=receiverId;nativeListener.callable="@AudioSource.SetVolume";
        InspectorEventArgument volumeArg;volumeArg.kind=PIN_NUM;volumeArg.value.x=1.75f;persistent.listeners.push_back(nativeListener);persistent.listeners.back().arguments.push_back(volumeArg);
        owner->inspectorEvents.push_back(persistent);
        bpInvokeInspectorEventCb(owner,"OnPressed");
        bool runtimeOk=fabsf(g.bpScripts[0].inst.vars["FunctionValue"].single.asNum()-12)<.001f&&
                       fabsf(g.bpScripts[0].inst.vars["EventValue"].single.asNum()-34)<.001f&&fabsf(s.byId(receiverId)->audioVolume-1.75f)<.001f;
        EditorScene eventRoundtrip;bool sceneOk=eventRoundtrip.deserialize(s.serialize())&&eventRoundtrip.byId(ownerId)&&
            eventRoundtrip.byId(ownerId)->hasInspectorEvents&&eventRoundtrip.byId(ownerId)->inspectorEvents.size()==1&&
            eventRoundtrip.byId(ownerId)->inspectorEvents[0].listeners.size()==3&&
            eventRoundtrip.byId(ownerId)->inspectorEvents[0].listeners[1].arguments[0].value.x==34;
        bool shapeOk=bpDefs()[BP_INVOKE_INSPECTOR_EVENT].ins[1].kind==PIN_ENT&&bpDefs()[BP_INVOKE_INSPECTOR_EVENT].ins[2].kind==PIN_STR;
        bool ok=runtimeOk&&sceneOk&&shapeOk;snprintf(detail,sizeof(detail),"multi-listener=%s, argomenti funzione/evento=%s, roundtrip=%s",runtimeOk?"ok":"NO",runtimeOk?"ok":"NO",sceneOk?"ok":"NO");
        report("Persistent Inspector Events",ok,detail);if(!ok)failures++;g.bpScripts.clear();
    }

    {
        // Blueprint v20: trigger phases, mesh query colliders and component delegate binding.
        EditorScene overlapScene;
        int triggerId = overlapScene.spawnBox("trigger", { 0, 0, 0 }, { 2, 2, 2 }, {}, BodyType::Static, 0).id;
        Entity* trigger = overlapScene.byId(triggerId);
        trigger->hasMesh = false; trigger->hasPhysics = false; trigger->hasTrigger = true; trigger->collision = 1; trigger->triggerShape = 1;
        overlapScene.syncBodyShape(*trigger);
        int moverId = overlapScene.spawnSphere("mover", { 0, 0, 0 }, 1, {}, 1).id;
        trigger = overlapScene.byId(triggerId);
        Entity* mover = overlapScene.byId(moverId);
        mover->body->useGravity = false; mover->body->canSleep = false;
        overlapScene.world.step(FIXED_DT);
        bool beginPhysics = overlapScene.world.contactEvents.empty() && overlapScene.world.overlapEvents.size() == 1 &&
                            overlapScene.world.overlapEvents[0].begin && trigger->body->shape.kind == ShapeKind::Sphere;
        mover->body->position = { 8, 0, 0 }; mover->body->updateAABB();
        overlapScene.world.step(FIXED_DT);
        bool endPhysics = overlapScene.world.overlapEvents.size() == 1 && !overlapScene.world.overlapEvents[0].begin;
        trigger->triggerShape = 2; overlapScene.syncBodyShape(*trigger);
        bool capsuleTrigger = trigger->body->shape.kind == ShapeKind::Capsule;
        int meshId = overlapScene.spawnBox("mesh query", { 20, 0, 0 }, { 1, 3, 1 }, {}, BodyType::Dynamic, 1).id;
        Entity* meshQuery = overlapScene.byId(meshId);
        meshQuery->mesh = MESH_CAPSULE; meshQuery->hasPhysics = false; overlapScene.syncBodyShape(*meshQuery);
        bool meshCollider = meshQuery->body->enabled && meshQuery->body->trigger && meshQuery->body->queryOnly &&
                            meshQuery->body->shape.kind == ShapeKind::Capsule;
        EditorScene overlapRoundtrip;
        bool sceneOk = overlapRoundtrip.deserialize(overlapScene.serialize()) && overlapRoundtrip.byId(triggerId) &&
                       overlapRoundtrip.byId(triggerId)->hasTrigger && overlapRoundtrip.byId(triggerId)->triggerShape == 2 && overlapRoundtrip.byId(meshId) &&
                       overlapRoundtrip.byId(meshId)->body->queryOnly;

        BPGraph graph;
        auto actorVar = [&](const char* name) { BPVarDef value; snprintf(value.name, sizeof(value.name), "%s", name); value.type = PIN_ENT; graph.vars.push_back(value); };
        actorVar("beginOther"); actorVar("endOther"); actorVar("boundBegin"); actorVar("boundEnd");
        auto addSignature = [&](const char* name) {
            BPEventDef signature; snprintf(signature.name, sizeof(signature.name), "%s", name);
            BPFuncPin component; snprintf(component.name, sizeof(component.name), "Component"); component.kind = PIN_ENT;
            BPFuncPin other; snprintf(other.name, sizeof(other.name), "OtherActor"); other.kind = PIN_ENT;
            signature.params = { component, other }; graph.events.push_back(signature);
        };
        addSignature("BoundBegin"); addSignature("BoundEnd");
        BPCanvas& canvas = graph.main();
        int start = canvas.addNode(BP_EV_START, 0, 0);
        int sequence = canvas.addNode(BP_FLOW_SEQ, 100, 0);
        int bindBegin = canvas.addNode(BP_BIND_BEGIN_OVERLAP, 220, 0); canvas.byId(bindBegin)->lit[1].x = (float)meshId;
        int bindEnd = canvas.addNode(BP_BIND_END_OVERLAP, 220, 100); canvas.byId(bindEnd)->lit[1].x = (float)meshId;
        int customBegin = canvas.addNode(BP_EV_CUSTOM, 0, 220); snprintf(canvas.byId(customBegin)->sname, sizeof(canvas.byId(customBegin)->sname), "BoundBegin");
        int customEnd = canvas.addNode(BP_EV_CUSTOM, 0, 340); snprintf(canvas.byId(customEnd)->sname, sizeof(canvas.byId(customEnd)->sname), "BoundEnd");
        int setBoundBegin = canvas.addNode(BP_VAR_SET, 260, 220); snprintf(canvas.byId(setBoundBegin)->sname, sizeof(canvas.byId(setBoundBegin)->sname), "boundBegin");
        int setBoundEnd = canvas.addNode(BP_VAR_SET, 260, 340); snprintf(canvas.byId(setBoundEnd)->sname, sizeof(canvas.byId(setBoundEnd)->sname), "boundEnd");
        int beginEvent = canvas.addNode(BP_EV_BEGIN_OVERLAP, 500, 0);
        int endEvent = canvas.addNode(BP_EV_END_OVERLAP, 500, 140);
        int setBegin = canvas.addNode(BP_VAR_SET, 760, 0); snprintf(canvas.byId(setBegin)->sname, sizeof(canvas.byId(setBegin)->sname), "beginOther");
        int setEnd = canvas.addNode(BP_VAR_SET, 760, 140); snprintf(canvas.byId(setEnd)->sname, sizeof(canvas.byId(setEnd)->sname), "endOther");
        canvas.connect(start, 0, sequence, 0);
        canvas.connect(sequence, 0, bindBegin, 0); canvas.connect(customBegin, 3, bindBegin, 2);
        canvas.connect(sequence, 1, bindEnd, 0); canvas.connect(customEnd, 3, bindEnd, 2);
        canvas.connect(customBegin, 0, setBoundBegin, 0); canvas.connect(customBegin, 2, setBoundBegin, 1);
        canvas.connect(customEnd, 0, setBoundEnd, 0); canvas.connect(customEnd, 2, setBoundEnd, 1);
        canvas.connect(beginEvent, 0, setBegin, 0); canvas.connect(beginEvent, 2, setBegin, 1);
        canvas.connect(endEvent, 0, setEnd, 0); canvas.connect(endEvent, 2, setEnd, 1);
        BPGraph graphRoundtrip;
        bool graphOk = graphRoundtrip.deserialize(graph.serialize());
        Entity* component = overlapScene.byId(meshId);
        BPInstance instance; instance.graph = &graphRoundtrip; instance.entity = component; instance.initVars(nullptr);
        BPContext context; context.entity = component; context.scene = &overlapScene;
        instance.fire(BP_EV_START, context);
        context.eventOther = 71; instance.fire(BP_EV_BEGIN_OVERLAP, context);
        context.eventOther = 72; instance.fire(BP_EV_END_OVERLAP, context);
        instance.fireOverlapBinding(true, meshId, 81, context);
        instance.fireOverlapBinding(false, meshId, 82, context);
        bool eventsOk = instance.vars["beginOther"].single.asEnt() == 71 && instance.vars["endOther"].single.asEnt() == 72 &&
                        instance.vars["boundBegin"].single.asEnt() == 81 && instance.vars["boundEnd"].single.asEnt() == 82;
        bool shapesOk = bpDefs()[BP_EV_BEGIN_OVERLAP].outs[2].kind == PIN_ENT &&
                        bpDefs()[BP_EV_END_OVERLAP].outs[2].kind == PIN_ENT &&
                        bpDefs()[BP_BIND_BEGIN_OVERLAP].ins[2].kind == PIN_DELEGATE;
        bool ok = beginPhysics && endPhysics && capsuleTrigger && meshCollider && sceneOk && graphOk && eventsOk && shapesOk;
        snprintf(detail, sizeof(detail), "physics begin/end=%s, trigger/mesh=%s, events/delegates=%s",
                 beginPhysics && endPhysics ? "ok" : "NO", capsuleTrigger && meshCollider && sceneOk ? "ok" : "NO",
                 graphOk && eventsOk && shapesOk ? "ok" : "NO");
        report("Blueprint v20 (Overlap / Trigger shapes / Mesh delegates)", ok, detail);
        if (!ok) failures++;
    }

    {
        // Prefab provenance survives the level and a saved asset refreshes all instances.
        s.clear();EditorScene prefabAsset;Entity& prefabRoot=prefabAsset.spawnBox("Crate",{0,.5f,0},{1,1,1},{.2f,.4f,.8f},BodyType::Static,0);
        int prefabRootId=prefabRoot.id;prefabAsset.spawnEmpty("Marker",{0,1.5f,0}).parentId=prefabRootId;
        std::string rel="prefab\\Crate.pfb",firstData=prefabAsset.serialize();
        s.spawnEmpty("Before",{});
        std::vector<int> first=s.instantiateFrom(firstData,{-3,0,0},true);int firstRoot=markPrefabInstance(first,rel);
        s.spawnEmpty("Between",{});
        std::vector<int> second=s.instantiateFrom(firstData,{3,0,0},true);int secondRoot=markPrefabInstance(second,rel);
        s.spawnEmpty("After",{});
        Vec3 firstPos=s.byId(firstRoot)->body->position,secondPos=s.byId(secondRoot)->body->position;
        prefabAsset.byId(prefabRootId)->color={.9f,.2f,.3f};prefabAsset.spawnEmpty("NewChild",{0,2.5f,0}).parentId=prefabRootId;
        propagatePrefabInstances(rel,prefabAsset.serialize());
        int roots=0,members=0;bool poses=false,colors=true;
        for(const Entity&e:s.entities)if(_stricmp(e.prefabAsset,rel.c_str())==0){members++;if(e.prefabInstanceRoot==e.id){roots++;poses=poses||e.body->position.distanceTo(firstPos)<.01f||e.body->position.distanceTo(secondPos)<.01f;colors=colors&&e.color.distanceTo({.9f,.2f,.3f})<.01f;}}
        std::vector<std::string> rootOrder;for(const Entity&e:s.entities)if(e.parentId==0)rootOrder.push_back(e.name);
        bool orderOk=rootOrder.size()==5&&rootOrder[0]=="Before"&&rootOrder[1]=="Crate"&&rootOrder[2]=="Between"&&rootOrder[3]=="Crate"&&rootOrder[4]=="After";
        EditorScene prefabRoundtrip;bool persisted=prefabRoundtrip.deserialize(s.serialize())&&prefabRoundtrip.entities.size()==s.entities.size();
        bool ok=roots==2&&members==6&&poses&&colors&&orderOk&&persisted;
        snprintf(detail,sizeof(detail),"istanze=%d, membri blu=%d, ordine=%s, aggiornamento=%s, roundtrip=%s",roots,members,orderOk?"ok":"NO",colors?"ok":"NO",persisted?"ok":"NO");
        report("Prefab Mode / propagazione istanze",ok,detail);if(!ok)failures++;
    }

    {
        // A Transform-driven static character must not teleport through walls.
        EditorScene characterScene;
        Entity& character=characterScene.spawnBox("Character",{-2,.5f,0},{1,1,1},{1,1,1},BodyType::Static,0);
        int characterId=character.id; character.hasPhysics=true; character.collision=0; characterScene.syncBodyShape(character);
        Entity& wall=characterScene.spawnBox("Wall",{0,.5f,0},{1,3,4},{1,1,1},BodyType::Static,0);
        wall.hasPhysics=false;wall.collision=0;characterScene.syncBodyShape(wall);
        BPGraph moveGraph;int begin=moveGraph.main().addNode(BP_EV_START,0,0);int move=moveGraph.main().addNode(BP_SET_WLOC,160,0);
        moveGraph.main().byId(move)->lit[2]={2,.5f,0};moveGraph.main().connect(begin,0,move,0);
        BPInstance moveInstance;moveInstance.graph=&moveGraph;moveInstance.entity=characterScene.byId(characterId);moveInstance.initVars(nullptr);
        BPContext moveContext;moveContext.entity=moveInstance.entity;moveContext.scene=&characterScene;moveContext.dt=1.0f/60.0f;
        moveInstance.fire(BP_EV_START,moveContext);
        float stoppedX=characterScene.byId(characterId)->body->position.x;
        bool ok=stoppedX<-0.94f&&stoppedX>-1.15f&&characterScene.byId(characterId)->body->velocity.x>0;
        snprintf(detail,sizeof(detail),"sweep contro Mesh statica: x=%.3f, velocity=%.2f",stoppedX,characterScene.byId(characterId)->body->velocity.x);
        report("Character statico / Set Location con collisione",ok,detail);if(!ok)failures++;
    }

    {
        // Navigation / AI / Reverb: component state survives scene round-trip,
        // the baked grid produces an A* path and a non-physics agent can follow it.
        s.clear();
        Entity& ground = s.spawnBox("nav_ground", { 0, -0.5f, 0 }, { 12, 1, 8 }, {}, BodyType::Static, 0);
        ground.staticFlags = STATIC_MOVEMENT | STATIC_LIGHTING | STATIC_NAVIGATION;
        ground.doubleSided = true;
        Entity& zone = s.spawnEmpty("reverb_zone", { 0, 0, 0 });
        zone.hasReverb = true; zone.reverbRadius = 9.0f; zone.reverbWet = 0.7f; zone.reverbDecay = 2.2f;
        Entity& source = s.spawnEmpty("audio_in_zone", { 1, 0, 0 });
        source.hasAudio = true; source.audioSpatial = false; source.audioVolume = 1.0f;
        source.isCamera=true;source.camLinearClipping=true;source.camNearClip=.25f;source.camClipDistance=180.0f;source.camLayerMask=0x0005u;
        int sourceId = source.id;
        Entity& wall = s.spawnBox("nav_wall", { 0, 0.5f, 0 }, { 0.8f, 1.0f, 3.0f }, {}, BodyType::Static, 0);
        wall.staticFlags = STATIC_MOVEMENT;
        wall.hasNavigationOccluder = true;
        wall.navigationOccluderPadding = 0.1f;
        Entity& agent = s.spawnEmpty("nav_agent", { -4, 0.5f, 0 });
        int agentId = agent.id;
        agent.hasAIAgent = true; agent.aiSpeed = 3.0f; agent.aiAcceleration = 20.0f;agent.aiBaseOffset=1.25f;
        agent.aiDebugDraw = false;
        g.navigation.cellSize = 0.5f; g.navigation.stepHeight = 0.6f;
        bakeNavigation();
        std::vector<Vec3> path;
        bool pathOk = findNavigationPath(agent.body->position, { 4, 0.5f, 0 }, path) && path.size() > 2;
        aiSetDestination(agent, { 4, 0.5f, 0 });
        for (int i = 0; i < 360; i++) updateAIAgents(FIXED_DT);
        float finalX = s.byId(agentId)->body->position.x;
        float finalY = s.byId(agentId)->body->position.y;
        float wetGain = audioSourceGain(*s.byId(sourceId), {});
        std::string serialized = s.serialize();
        EditorScene loaded;
        bool sceneOk = loaded.deserialize(serialized) && loaded.entities.size() == 5 &&
                       loaded.entities[0].staticFlags == (STATIC_MOVEMENT | STATIC_LIGHTING | STATIC_NAVIGATION) &&
                       loaded.entities[0].doubleSided && loaded.entities[1].hasReverb && loaded.entities[3].hasNavigationOccluder &&
                       fabsf(loaded.entities[3].navigationOccluderPadding-.1f)<.001f && loaded.entities[4].hasAIAgent &&
                       fabsf(loaded.entities[4].aiBaseOffset-1.25f)<.001f && !loaded.entities[4].aiDebugDraw && loaded.entities[2].isCamera &&
                       loaded.entities[2].camLayerMask==0x0005u && fabsf(loaded.entities[2].camNearClip-.25f)<.001f &&
                       fabsf(loaded.entities[2].camClipDistance-180.0f)<.001f;
        bool nodeShapes = BP_NCOMPS == 11 && bpDefs()[BP_AI_SET_TARGET].ins[2].kind == PIN_ENT &&
                          bpDefs()[BP_AI_SET_DESTINATION].ins[2].kind == PIN_VEC &&
                          bpDefs()[BP_AI_HAS_PATH].outs[0].kind == PIN_BOOL;
        bool ok = g.navigation.baked && pathOk && finalX > 2.5f && fabsf(finalY-1.25f)<.05f && wetGain > 1.05f && sceneOk && nodeShapes;
        snprintf(detail, sizeof(detail), "bake/path=%s, agent=(%.2f, %.2f), reverb gain=%.2f, scene/nodes=%s",
                 pathOk ? "ok" : "NO", finalX,finalY, wetGain, sceneOk && nodeShapes ? "ok" : "NO");
        report("Navigation / AI Agent / Reverb / Static flags", ok, detail);
        if (!ok) failures++;
    }

    {
        // Animation assets and viewport-as-a-tab regression coverage.
        AnimationClipAsset clip; clip.length = 99; clip.loop = false;
        AnimationKey a; a.entityId=11; a.objectName="Empty Player"; a.localSpace=true; a.time = 0; a.position = {0,0,0}; a.rotation = Quat::fromEulerDeg(0,0,0); a.scale = {1,1,1};
        AnimationKey b; b.entityId=11; b.objectName="Empty Player"; b.time = 2; b.position = {4,2,0}; b.rotation = Quat::fromEulerDeg(0,0,90); b.scale = {2,2,2};
        AnimationKey c=a; c.entityId=22; c.objectName="Light Rig"; c.position={10,0,0};
        AnimationKey d=b; d.entityId=22; d.objectName="Light Rig"; d.position={14,0,0};
        clip.keys = {d,a,c,b};
        clip.events={{.25f,"Footstep"},{1.5f,"SwordImpact"}};
        AnimationClipAsset clip2; bool clipRoundtrip = clip2.deserialize(clip.serialize());
        AnimationKey middle = clip2.evaluateTrack(1,11,"Empty Player");
        AnimationKey lightMiddle = clip2.evaluateTrack(1,22,"Light Rig");
        AnimationClipAsset previewLoop=clip2;previewLoop.loop=true;
        AnimationKey heldPastEnd=previewLoop.evaluateTrack(50,11,"Empty Player",false);
        AnimationKey wrappedPastEnd=previewLoop.evaluateTrack(50,11,"Empty Player",true);
        bool eventAssetOk=clip2.events.size()==2&&clip2.events[0].name=="Footstep"&&fabsf(clip2.events[1].time-1.5f)<.001f;
        bool durationOk=fabsf(clip2.length-2.0f)<.001f;
        AnimationClipAsset shortened=clip2;
        shortened.keys.erase(std::remove_if(shortened.keys.begin(),shortened.keys.end(),[](const AnimationKey&k){return k.time>0.5f;}),shortened.keys.end());
        shortened.events.clear();
        shortened.sortKeys();
        durationOk=durationOk&&fabsf(shortened.length-.01f)<.001f;
        AnimationClipAsset legacyClip;
        bool legacyOk=legacyClip.deserialize("IMPULSOANIM 1\nlength 1\nloop 0\nkey 0 1 2 3 0 0 0 1 1 1 1\n");
        bool evalOk = middle.localSpace&&fabsf(middle.position.x-2)<.001f && fabsf(middle.position.y-1)<.001f && fabsf(middle.scale.x-1.5f)<.001f &&
                      fabsf(lightMiddle.position.x-12)<.001f && lightMiddle.objectName=="Light Rig" &&
                      fabsf(heldPastEnd.position.x-4)<.001f && fabsf(wrappedPastEnd.position.x)<.001f && legacyOk;
        AnimatorControllerAsset controller;
        int idle=controller.defaultState;AnimatorState* idleState=controller.byId(idle);idleState->name="Idle";idleState->clip="Idle.anim";idleState->x=20;idleState->y=30;
        int run=controller.addState("Run","Run.anim",240,30);
        bool linkOk=controller.connect(idle,run);
        AnimatorParameter speedParam;speedParam.name="Speed";speedParam.type=ANIM_PARAM_FLOAT;controller.parameters.push_back(speedParam);
        AnimatorTransition* runTransition=controller.transition(idle,run);runTransition->duration=.35f;runTransition->blendCurve=1;runTransition->parameter="Speed";runTransition->condition=3;runTransition->threshold=.1f;
        controller.byId(run)->mirror=true;
        AnimatorControllerAsset controller2;
        bool controllerRoundtrip=controller2.deserialize(controller.serialize()) && controller2.states.size()==2 &&
                                 controller2.transitions.size()==1 && controller2.defaultState==idle&&controller2.parameters.size()==1&&
                                 controller2.transition(idle,run)&&fabsf(controller2.transition(idle,run)->duration-.35f)<.001f&&controller2.byId(run)->mirror;
        BPGraph animationRefGraph;
        BPVarDef clipRef; snprintf(clipRef.name,sizeof(clipRef.name),"AttackClip"); clipRef.type=PIN_ANIMATION_CLIP;
        snprintf(clipRef.assetPath,sizeof(clipRef.assetPath),"Animations\\Attack.anim");
        BPVarDef controllerRef; snprintf(controllerRef.name,sizeof(controllerRef.name),"Locomotion"); controllerRef.type=PIN_ENT;
        snprintf(controllerRef.refClass,sizeof(controllerRef.refClass),"asset:AnimatorController");
        snprintf(controllerRef.assetPath,sizeof(controllerRef.assetPath),"Animations\\Player.animctrl");
        animationRefGraph.vars={clipRef,controllerRef}; BPGraph animationRefRoundtrip;
        bool animationRefsOk=animationRefRoundtrip.deserialize(animationRefGraph.serialize())&&animationRefRoundtrip.vars.size()==2&&
                             animationRefRoundtrip.vars[0].type==PIN_ANIMATION_CLIP&&animationRefRoundtrip.vars[1].type==PIN_ENT&&
                             strcmp(animationRefRoundtrip.vars[1].refClass,"asset:AnimatorController")==0&&
                             strcmp(animationRefRoundtrip.vars[0].assetPath,"Animations\\Attack.anim")==0&&
                             strcmp(animationRefRoundtrip.vars[1].assetPath,"Animations\\Player.animctrl")==0;
        BPInstance animationRefInstance;animationRefInstance.graph=&animationRefRoundtrip;animationRefInstance.initVars(nullptr);
        animationRefsOk=animationRefsOk&&animationRefInstance.vars["AttackClip"].single.kind==PIN_ANIMATION_CLIP&&
                        animationRefInstance.vars["AttackClip"].single.str=="Animations\\Attack.anim"&&
                        animationRefInstance.vars["Locomotion"].single.kind==PIN_ENT&&
                        animationRefInstance.vars["Locomotion"].single.str=="Animations\\Player.animctrl";
        BPGraph legacyControllerGraph,legacyControllerRoundtrip;BPVarDef legacyControllerRef;
        snprintf(legacyControllerRef.name,sizeof(legacyControllerRef.name),"LegacyController");legacyControllerRef.type=PIN_ANIMATOR_CONTROLLER;
        snprintf(legacyControllerRef.assetPath,sizeof(legacyControllerRef.assetPath),"Animations\\Legacy.animctrl");legacyControllerGraph.vars.push_back(legacyControllerRef);
        animationRefsOk=animationRefsOk&&legacyControllerRoundtrip.deserialize(legacyControllerGraph.serialize())&&
                        legacyControllerRoundtrip.vars.size()==1&&legacyControllerRoundtrip.vars[0].type==PIN_ENT&&
                        strcmp(legacyControllerRoundtrip.vars[0].refClass,"asset:AnimatorController")==0&&
                        strcmp(legacyControllerRoundtrip.vars[0].assetPath,"Animations\\Legacy.animctrl")==0;
        Entity parameterOwner;parameterOwner.hasAnimator=true;bpSetAnimatorParameterCb(&parameterOwner,"Speed",ANIM_PARAM_FLOAT,.75f);
        bool parameterRuntimeOk=fabsf(parameterOwner.animatorRuntimeParameters["Speed"]-.75f)<.001f&&
                                animatorTransitionCondition(parameterOwner,*controller2.transition(idle,run));
        EditorScene providerScene; Entity& onlyBody=providerScene.spawnEmpty("rigid only"); onlyBody.hasPhysics=true;
        providerScene.syncBodyShape(onlyBody);
        bool providerOk=!onlyBody.body->enabled;
        DockManager dock; dock.addWindow("viewport","Viewport",DOCK_CENTER,0); dock.addWindow("log","Log",DOCK_CENTER,1);
        UIRect visible=dock.viewportRect(1200,800,60); dock.setActive("log"); UIRect hidden=dock.viewportRect(1200,800,60);
        dock.setActive("viewport"); UIRect restored=dock.viewportRect(1200,800,60);
        DockManager sized; sized.addWindow("viewport","Viewport",DOCK_CENTER,0); sized.addWindow("log","Log",DOCK_CENTER,1);
        sized.loadLayout("LAYOUT 5\nsizes 0 0 0\nprimarysplit 501 0.7\nwin viewport 5 0 0 0 1 0 0 400 300\nwin log 5 1 0 0 1 0 0 400 300\n");
        UIRect sizedViewport=sized.viewportRect(1200,800,60);
        bool dockOk=visible.w>100&&visible.h>100&&hidden.w==0&&restored.w==visible.w&&
                    fabsf(sizedViewport.w-840)<2&&sized.saveLayout().find("primarysplit 501")!=std::string::npos;
        s.clear();Entity& animated=s.spawnEmpty("RuntimeRoot",{10,3,0});
        animated.hasAnimator=true;animated.animatorPlayOnAwake=true;animated.animatorSpeed=1.5f;
        snprintf(animated.animatorController,sizeof(animated.animatorController),"Animations\\Player.animctrl");
        AnimationClipAsset runtimeClip;runtimeClip.loop=true;
        // The recorded ID deliberately differs; runtime resolves the owner by
        // its recorded name, as it would for another prefab instance.
        AnimationKey rk0;rk0.entityId=999;rk0.objectName="RuntimeRoot";rk0.time=0;rk0.position={100,20,0};rk0.scale={1,1,1};
        AnimationKey rk1=rk0;rk1.time=2;rk1.position={104,22,0};rk1.scale={2,2,2};runtimeClip.keys={rk0,rk1};runtimeClip.sortKeys();
        g.runtimeAnimatorBases.clear();applyRuntimeAnimation(animated,runtimeClip,1);
        bool runtimeAnimatorOk=animated.body->position.distanceTo({12,4,0})<.001f&&animated.scale.distanceTo({1.5f,1.5f,1.5f})<.001f;
        std::string animatedText=s.serialize();EditorScene animatedLoaded;
        runtimeAnimatorOk=runtimeAnimatorOk&&animatedLoaded.deserialize(animatedText)&&animatedLoaded.entities[0].hasAnimator&&
                          animatedLoaded.entities[0].animatorPlayOnAwake&&fabsf(animatedLoaded.entities[0].animatorSpeed-1.5f)<.001f&&
                          strcmp(animatedLoaded.entities[0].animatorController,"Animations\\Player.animctrl")==0;
        s.clear();
        int hierarchyOwnerId=s.spawnEmpty("RigRoot",{}).id;
        int staleIdChildId=s.spawnEmpty("OldIdTarget",{2,0,0}).id;
        int namedChildId=s.spawnEmpty("AnimatedArm",{10,0,0}).id;
        int outsideId=s.spawnEmpty("AnimatedArm",{30,0,0}).id;
        s.setParent(staleIdChildId,hierarchyOwnerId);s.setParent(namedChildId,hierarchyOwnerId);
        g.animationEdit.keys.clear();g.selectedId=namedChildId;AnimationKey recordedLocal=selectedAnimationKey();
        bool localRecordingOk=recordedLocal.localSpace&&fabsf(recordedLocal.position.x-10.0f)<.001f;
        AnimationClipAsset hierarchyClip;hierarchyClip.loop=false;
        AnimationKey hk0;hk0.entityId=staleIdChildId;hk0.objectName="AnimatedArm";hk0.localSpace=true;hk0.time=0;hk0.position={100,0,0};hk0.scale={1,1,1};
        AnimationKey hk1=hk0;hk1.time=1;hk1.position={104,0,0};hierarchyClip.keys={hk0,hk1};hierarchyClip.sortKeys();
        g.runtimeAnimatorBases.clear();applyRuntimeAnimation(*s.byId(hierarchyOwnerId),hierarchyClip,.5f);
        Vec3 parentMove{5,0,0};s.byId(hierarchyOwnerId)->body->position+=parentMove;s.moveDescendants(hierarchyOwnerId,parentMove);
        applyRuntimeAnimation(*s.byId(hierarchyOwnerId),hierarchyClip,.5f);
        bool followsTranslation=fabsf(s.byId(staleIdChildId)->body->position.x-7.0f)<.001f&&
                                fabsf(s.byId(namedChildId)->body->position.x-17.0f)<.001f;
        Entity* hierarchyOwner=s.byId(hierarchyOwnerId);Quat oldParentRotation=hierarchyOwner->body->quat;
        Quat turnedParent=Quat::fromEulerDeg(0,0,90);s.rotateDescendants(hierarchyOwnerId,hierarchyOwner->body->position,oldParentRotation,turnedParent);hierarchyOwner->body->quat=turnedParent;
        Vec3 oldParentScale=hierarchyOwner->scale,newParentScale{2,2,2};s.scaleDescendants(hierarchyOwnerId,hierarchyOwner->body->position,turnedParent,oldParentScale,newParentScale);hierarchyOwner->scale=newParentScale;
        applyRuntimeAnimation(*hierarchyOwner,hierarchyClip,.5f);
        Vec3 expectedAnimated=hierarchyOwner->body->position+turnedParent.rotate(mulComponents({12,0,0},newParentScale));
        bool hierarchyBindingOk=localRecordingOk&&followsTranslation&&
                                s.byId(namedChildId)->body->position.distanceTo(expectedAnimated)<.001f&&
                                fabsf(s.byId(outsideId)->body->position.x-30.0f)<.001f;
        Quat localRotation=Quat::fromEulerDeg(0,0,90);g.gizmoLocal=true;Vec3 localAxis=gizmoAxisFor(localRotation,0);
        g.gizmoLocal=false;Vec3 worldAxis=gizmoAxisFor(localRotation,0);
        bool transformSpaceOk=localAxis.distanceTo(localRotation.rotate({1,0,0}))<.001f&&worldAxis.distanceTo({1,0,0})<.001f;
        s.clear(); Entity& reset=s.spawnBox("reset",{4,5,6},{2,3,4},{1,0,0},BodyType::Dynamic,7);
        reset.mesh=MESH_CAPSULE; reset.varOverrides["Speed"]={99,0,0}; reset.body->friction=1.7f;
        resetComponentDefaults(reset,DETAIL_TRANSFORM); resetComponentDefaults(reset,DETAIL_MESH);
        resetComponentDefaults(reset,DETAIL_PHYSICS); resetComponentDefaults(reset,DETAIL_BLUEPRINT);
        bool resetOk=reset.body->position.length()<.001f&&reset.scale.distanceTo({1,1,1})<.001f&&reset.mesh==MESH_CUBE&&
                     fabsf(reset.body->mass-1)<.001f&&fabsf(reset.body->friction-.5f)<.001f&&reset.varOverrides.empty();

        // An empty default state must behave as a routing state. This is the
        // exact Enter -> "No clip" -> animated state setup used by the
        // Animator graph and used to stall because updateAnimators skipped it.
        s.clear();
        g.runtimeAnimatorControllers.clear();g.runtimeAnimationClips.clear();g.runtimeAnimatorBases.clear();
        AnimatorControllerAsset routingController;
        int routingStart=routingController.defaultState;
        routingController.byId(routingStart)->name="Routing";
        routingController.byId(routingStart)->clip.clear();
        int routingTarget=routingController.addState("Attack","Animations\\Routed.anim",260,80);
        routingController.connect(routingStart,routingTarget);
        g.runtimeAnimatorControllers["Animations\\Routing.animctrl"]=routingController;
        AnimationClipAsset routedClip;routedClip.loop=false;
        AnimationKey route0;route0.entityId=500;route0.objectName="RoutingOwner";route0.time=0;route0.position={0,0,0};route0.scale={1,1,1};
        AnimationKey route1=route0;route1.time=1;route1.position={2,0,0};
        routedClip.keys={route0,route1};routedClip.events.push_back({.25f,"Footstep"});routedClip.sortKeys();
        g.runtimeAnimationClips["Animations\\Routed.anim"]=routedClip;
        Entity& routingOwner=s.spawnEmpty("RoutingOwner",{});
        routingOwner.hasAnimator=true;routingOwner.animatorRuntimePlaying=true;routingOwner.animatorRuntimeState=routingStart;
        snprintf(routingOwner.animatorController,sizeof(routingOwner.animatorController),"Animations\\Routing.animctrl");
        BPGraph triggerGraph;BPVarDef triggerFired;snprintf(triggerFired.name,sizeof(triggerFired.name),"Triggered");triggerFired.type=PIN_BOOL;triggerGraph.vars.push_back(triggerFired);
        BPEventDef triggerSignature;snprintf(triggerSignature.name,sizeof(triggerSignature.name),"OnFootstep");triggerGraph.events.push_back(triggerSignature);
        BPCanvas& triggerCanvas=triggerGraph.main();int triggerEvent=triggerCanvas.addNode(BP_EV_CUSTOM,0,0);snprintf(triggerCanvas.byId(triggerEvent)->sname,sizeof(triggerCanvas.byId(triggerEvent)->sname),"OnFootstep");
        int triggerSet=triggerCanvas.addNode(BP_VAR_SET,180,0);snprintf(triggerCanvas.byId(triggerSet)->sname,sizeof(triggerCanvas.byId(triggerSet)->sname),"Triggered");triggerCanvas.byId(triggerSet)->lit[1].x=1;triggerCanvas.connect(triggerEvent,0,triggerSet,0);
        g.bpScripts.clear();g.animationTriggerBindings.clear();App::LiveScript triggerLive;triggerLive.entityId=routingOwner.id;triggerLive.inst.graph=&triggerGraph;triggerLive.inst.entity=&routingOwner;triggerLive.inst.initVars(nullptr);g.bpScripts.push_back(std::move(triggerLive));
        bpBindAnimationTriggerCb(&routingOwner,&routingOwner,"Footstep","OnFootstep");
        updateAnimators(.01f);updateAnimators(.5f);
        bool triggerRuntimeOk=!g.bpScripts.empty()&&g.bpScripts[0].inst.vars["Triggered"].single.asBool();
        bool emptyStateRoutingOk=routingOwner.animatorRuntimeState==routingTarget&&routingOwner.body->position.x>.9f&&triggerRuntimeOk;
        g.bpScripts.clear();g.animationTriggerBindings.clear();

        BPGraph conversionGraph;
        auto addConversionVar=[&](const char*name,PinKind kind){BPVarDef v;snprintf(v.name,sizeof(v.name),"%s",name);v.type=kind;conversionGraph.vars.push_back(v);};
        addConversionVar("FloatText",PIN_STR);addConversionVar("IntText",PIN_STR);addConversionVar("BoolText",PIN_STR);addConversionVar("Truncated",PIN_INT);
        BPCanvas& conversionCanvas=conversionGraph.main();
        int conversionStart=conversionCanvas.addNode(BP_EV_START,20,20);
        int floatToString=conversionCanvas.addNode(BP_FLOAT_TO_STRING,20,100);conversionCanvas.byId(floatToString)->lit[0].x=12.5f;
        int intToString=conversionCanvas.addNode(BP_INT_TO_STRING,20,150);conversionCanvas.byId(intToString)->lit[0].x=7;
        int boolToString=conversionCanvas.addNode(BP_BOOL_TO_STRING,20,200);conversionCanvas.byId(boolToString)->lit[0].x=1;
        int truncate=conversionCanvas.addNode(BP_M_TRUNCATE,20,250);conversionCanvas.byId(truncate)->lit[0].x=-3.8f;
        int setFloatText=conversionCanvas.addNode(BP_VAR_SET,240,100);snprintf(conversionCanvas.byId(setFloatText)->sname,sizeof(conversionCanvas.byId(setFloatText)->sname),"FloatText");
        int setIntText=conversionCanvas.addNode(BP_VAR_SET,400,100);snprintf(conversionCanvas.byId(setIntText)->sname,sizeof(conversionCanvas.byId(setIntText)->sname),"IntText");
        int setBoolText=conversionCanvas.addNode(BP_VAR_SET,560,100);snprintf(conversionCanvas.byId(setBoolText)->sname,sizeof(conversionCanvas.byId(setBoolText)->sname),"BoolText");
        int setTruncated=conversionCanvas.addNode(BP_VAR_SET,720,100);snprintf(conversionCanvas.byId(setTruncated)->sname,sizeof(conversionCanvas.byId(setTruncated)->sname),"Truncated");
        conversionCanvas.connect(conversionStart,0,setFloatText,0);conversionCanvas.connect(setFloatText,0,setIntText,0);
        conversionCanvas.connect(setIntText,0,setBoolText,0);conversionCanvas.connect(setBoolText,0,setTruncated,0);
        conversionCanvas.connect(floatToString,0,setFloatText,1);conversionCanvas.connect(intToString,0,setIntText,1);
        conversionCanvas.connect(boolToString,0,setBoolText,1);conversionCanvas.connect(truncate,0,setTruncated,1);
        BPInstance conversionInstance;conversionInstance.graph=&conversionGraph;conversionInstance.initVars(nullptr);BPContext conversionContext;
        conversionInstance.fire(BP_EV_START,conversionContext);
        bool conversionRuntimeOk=conversionInstance.vars["FloatText"].single.str=="12.5"&&conversionInstance.vars["IntText"].single.str=="7"&&
                                 conversionInstance.vars["BoolText"].single.str=="true"&&(int)conversionInstance.vars["Truncated"].single.num==-3;

        BPGraph colorGraph;BPVarDef tint;
        snprintf(tint.name,sizeof(tint.name),"Tint");tint.type=PIN_COLOR;tint.def={.2f,.4f,.6f};tint.defAlpha=.35f;
        colorGraph.vars.push_back(tint);
        int printNode=colorGraph.main().addNode(BP_ACT_PRINT,100,100);
        BPNode* print=colorGraph.main().byId(printNode);print->lit[2]={.8f,.3f,.1f};print->litAlpha[2]=.45f;
        BPGraph colorRoundtrip;bool colorGraphOk=colorRoundtrip.deserialize(colorGraph.serialize())&&colorRoundtrip.vars.size()==1&&
                          colorRoundtrip.vars[0].type==PIN_COLOR&&fabsf(colorRoundtrip.vars[0].defAlpha-.35f)<.001f&&
                          colorRoundtrip.main().byId(printNode)&&fabsf(colorRoundtrip.main().byId(printNode)->litAlpha[2]-.45f)<.001f;
        BPInstance colorInstance;colorInstance.graph=&colorRoundtrip;colorInstance.initVars(nullptr);
        colorGraphOk=colorGraphOk&&colorInstance.vars["Tint"].single.kind==PIN_COLOR&&
                     fabsf(colorInstance.vars["Tint"].single.alpha-.35f)<.001f;
        s.clear();Entity& alphaMaterial=s.spawnBox("Alpha material",{},{1,1,1},{1,1,1});alphaMaterial.colorAlpha=.42f;
        EditorScene alphaLoaded;bool materialAlphaOk=alphaLoaded.deserialize(s.serialize())&&
                       fabsf(alphaLoaded.entities[0].colorAlpha-.42f)<.001f;
        const BPNodeDef* defs=bpDefs();
        bool textNodeShapes=defs[BP_ACT_PRINT].ins[1].kind==PIN_STR&&defs[BP_ACT_PRINT].ins[2].kind==PIN_COLOR&&
                            defs[BP_FLOAT_TO_STRING].outs[0].kind==PIN_STR&&defs[BP_INT_TO_STRING].outs[0].kind==PIN_STR&&
                            defs[BP_BOOL_TO_STRING].outs[0].kind==PIN_STR&&defs[BP_M_TRUNCATE].outs[0].kind==PIN_INT;
        bool animatorNodeShapes=bpDefs()[BP_ANIM_SET_FLOAT].ins[2].kind==PIN_NUM&&bpDefs()[BP_ANIM_SET_BOOL].ins[2].kind==PIN_BOOL&&bpDefs()[BP_ANIM_SET_TRIGGER].nIns==2&&bpDefs()[BP_ANIM_BIND_TRIGGER].ins[2].kind==PIN_DELEGATE;
        bool ok=clipRoundtrip&&eventAssetOk&&evalOk&&durationOk&&linkOk&&controllerRoundtrip&&animationRefsOk&&parameterRuntimeOk&&animatorNodeShapes&&runtimeAnimatorOk&&hierarchyBindingOk&&emptyStateRoutingOk&&transformSpaceOk&&providerOk&&dockOk&&resetOk&&colorGraphOk&&materialAlphaOk&&textNodeShapes&&conversionRuntimeOk;
        snprintf(detail,sizeof(detail),"clip/eventi/controller=%s, runtime/trigger=%s, colore/testo=%s, dock/reset=%s",
                 clipRoundtrip&&eventAssetOk&&evalOk&&linkOk&&controllerRoundtrip?"ok":"NO",runtimeAnimatorOk&&hierarchyBindingOk&&emptyStateRoutingOk?"ok":"NO",
                 colorGraphOk&&materialAlphaOk&&textNodeShapes&&conversionRuntimeOk?"ok":"NO",dockOk&&resetOk?"ok":"NO");
        report("Animation / Animator / collision provider / viewport dock",ok,detail);
        if(!ok)failures++;
    }

    {
        // Audio settings assets: all three formats round-trip, scene references
        // survive serialization and class + attenuation affect the final gain.
        s.clear();
        AudioClassAsset cls; cls.volume = 0.5f;
        AudioClassAsset cls2;
        AudioAttenuationAsset atten; atten.spatial = true; atten.minDistance = 1.0f; atten.maxDistance = 9.0f; atten.falloff = 0;
        AudioAttenuationAsset atten2;
        AudioConcurrencyAsset concurrency; concurrency.maxVoices = 3; concurrency.resolution = 1;
        AudioConcurrencyAsset concurrency2;
        bool assetsOk = cls2.deserialize(cls.serialize()) && fabsf(cls2.volume - 0.5f) < 1e-5f &&
                        atten2.deserialize(atten.serialize()) && atten2.spatial &&
                        fabsf(atten2.minDistance - 1.0f) < 1e-5f && fabsf(atten2.maxDistance - 9.0f) < 1e-5f &&
                        concurrency2.deserialize(concurrency.serialize()) && concurrency2.maxVoices == 3 && concurrency2.resolution == 1;

        EditorScene audioScene;
        Entity& source = audioScene.spawnEmpty("audio_asset_source", { 5, 0, 0 });
        source.hasAudio = true;
        source.audioVolume = 1.5f;
        snprintf(source.audioClip, sizeof(source.audioClip), "Audio\\step.wav");
        snprintf(source.audioClass, sizeof(source.audioClass), "Audio\\SFX.aclass");
        snprintf(source.audioAttenuation, sizeof(source.audioAttenuation), "Audio\\World.atten");
        snprintf(source.audioConcurrency, sizeof(source.audioConcurrency), "Audio\\Footsteps.concurrency");
        std::string sceneText = audioScene.serialize();
        EditorScene loaded;
        bool sceneOk = loaded.deserialize(sceneText) && loaded.entities.size() == 1 &&
                       strcmp(loaded.entities[0].audioClass, source.audioClass) == 0 &&
                       strcmp(loaded.entities[0].audioAttenuation, source.audioAttenuation) == 0 &&
                       strcmp(loaded.entities[0].audioConcurrency, source.audioConcurrency) == 0;
        g.audioClassCache[source.audioClass] = cls;
        g.audioAttenuationCache[source.audioAttenuation] = atten;
        float gain = audioSourceGain(source, {}); // distance 5: linear attenuation 0.5
        bool gainOk = fabsf(gain - 0.375f) < 1e-4f; // 1.5 * class 0.5 * attenuation 0.5
        bool ok = assetsOk && sceneOk && gainOk;
        snprintf(detail, sizeof(detail), "assets=%s, scene refs=%s, gain=%.3f",
                 assetsOk ? "ok" : "NO", sceneOk ? "ok" : "NO", gain);
        report("Audio assets (Class / Attenuation / Concurrency)", ok, detail);
        if (!ok) failures++;
    }

    {
        // Multiple Blueprint components keep independent overrides, survive a
        // scene round-trip and each create their own runtime instance.
        g.bpScripts.clear();
        g.graphCache.clear();
        s.clear();
        Entity& owner = s.spawnEmpty("multi_blueprint_owner");
        addBlueprintComponent(owner, "Scripts\\Movement.bp");
        addBlueprintComponent(owner, "Scripts\\Health.bp");
        owner.varOverrides["Value"] = { 11, 0, 0 };
        owner.additionalBlueprints[0].varOverrides["Value"] = { 22, 0, 0 };
        owner.additionalBlueprints[0].collapsed = true;

        BPVarDef value; snprintf(value.name, sizeof(value.name), "Value");
        BPGraph movementGraph; movementGraph.vars.push_back(value);
        BPGraph healthGraph; healthGraph.vars.push_back(value);
        g.graphCache.emplace("Scripts\\Movement.bp", std::move(movementGraph));
        g.graphCache.emplace("Scripts\\Health.bp", std::move(healthGraph));
        int added = bpAddLiveScripts(owner.id);
        bool runtimeOk = added == 2 && g.bpScripts.size() == 2 &&
                         g.bpScripts[0].componentIndex == 0 && g.bpScripts[1].componentIndex == 1 &&
                         fabsf(g.bpScripts[0].inst.vars["Value"].single.asNum() - 11) < .001f &&
                         fabsf(g.bpScripts[1].inst.vars["Value"].single.asNum() - 22) < .001f;

        EditorScene loaded;
        bool serializedOk = loaded.deserialize(s.serialize()) && loaded.entities.size() == 1 &&
                            strcmp(loaded.entities[0].graphPath, "Scripts\\Movement.bp") == 0 &&
                            loaded.entities[0].additionalBlueprints.size() == 1 &&
                            loaded.entities[0].additionalBlueprints[0].graphPath == "Scripts\\Health.bp" &&
                            loaded.entities[0].additionalBlueprints[0].collapsed &&
                            fabsf(loaded.entities[0].additionalBlueprints[0].varOverrides["Value"].x - 22) < .001f;
        bool ok = runtimeOk && serializedOk;
        snprintf(detail, sizeof(detail), "runtime=%d istanze, override=%s, roundtrip=%s", added,
                 runtimeOk ? "separati" : "NO", serializedOk ? "ok" : "NO");
        report("Componenti Blueprint multipli", ok, detail);
        if (!ok) failures++;
        g.bpScripts.clear();
        g.graphCache.clear();
    }

    {
        BPGraph authored;
        authored.uniquePerObject = false;
        BPRequiredComponent mesh;
        mesh.kind = BP_REQ_MESH;
        snprintf(mesh.variableName, sizeof(mesh.variableName), "RenderedMesh");
        BPRequiredComponent behavior;
        behavior.kind = BP_REQ_BLUEPRINT;
        behavior.blueprintAsset = "Scripts\\Reusable Health.bp";
        snprintf(behavior.variableName, sizeof(behavior.variableName), "Health");
        authored.requiredComponents = { mesh, behavior };
        authored.syncRequiredVariables();

        BPGraph loaded;
        bool roundTrip = loaded.deserialize(authored.serialize()) &&
                         !loaded.uniquePerObject && loaded.requiredComponents.size() == 2 &&
                         loaded.vars.size() == 2 && loaded.vars[0].requiredGenerated &&
                         strcmp(loaded.vars[0].name, "RenderedMesh") == 0 &&
                         strcmp(loaded.vars[1].refClass, "blueprint:Scripts\\Reusable Health.bp") == 0;
        Entity owner;
        owner.id = 731;
        BPInstance instance;
        instance.graph = &loaded;
        instance.entity = &owner;
        instance.initVars(nullptr, nullptr);
        bool referenceOk = instance.vars.count("RenderedMesh") &&
                           instance.vars["RenderedMesh"].single.asEnt() == owner.id &&
                           instance.vars.count("Health") && instance.vars["Health"].single.asEnt() == owner.id;
        bool ok = roundTrip && referenceOk;
        snprintf(detail, sizeof(detail), "roundtrip=%s, refs=%s, unique=%s",
                 roundTrip ? "ok" : "NO", referenceOk ? "owner" : "NO",
                 loaded.uniquePerObject ? "si" : "no");
        report("Blueprint Required Components", ok, detail);
        if (!ok) failures++;
    }

    {
        // Blueprint Interface assets are signature-only. No-output declarations
        // materialize as Interface Events; declarations with outputs remain
        // functions with an immutable Entry -> Return layout in the .bpi editor.
        fs::path tempRoot = fs::path(g.baseDir) / "build" / "test_bpi_signature_only";
        std::error_code ec;
        fs::create_directories(tempRoot, ec);

        BPGraph legacyInterface;
        BPVarDef forbiddenVariable; snprintf(forbiddenVariable.name, sizeof(forbiddenVariable.name), "Forbidden");
        legacyInterface.vars.push_back(forbiddenVariable);
        BPDispatcherDef forbiddenDispatcher; snprintf(forbiddenDispatcher.name, sizeof(forbiddenDispatcher.name), "ForbiddenDispatcher");
        legacyInterface.dispatchers.push_back(forbiddenDispatcher);
        BPFunc forbiddenGraph; snprintf(forbiddenGraph.name, sizeof(forbiddenGraph.name), "ExtraGraph");
        forbiddenGraph.body.addNode(BP_ACT_PRINT, 0, 0);
        legacyInterface.graphs.push_back(forbiddenGraph);

        BPFunc notifySignature; snprintf(notifySignature.name, sizeof(notifySignature.name), "Notify");
        notifySignature.ins.clear(); notifySignature.outs.clear();
        BPFuncPin amount; snprintf(amount.name, sizeof(amount.name), "Amount"); amount.kind = PIN_NUM;
        notifySignature.ins.push_back(amount);
        int notifyEntry = notifySignature.body.addNode(BP_FN_ENTRY, 40, 70);
        int forbiddenNotifyNode = notifySignature.body.addNode(BP_ACT_PRINT, 180, 70);
        int legacyNotifyReturn = notifySignature.body.addNode(BP_FN_RETURN, 330, 70);
        notifySignature.body.connect(notifyEntry, 0, forbiddenNotifyNode, 0);
        notifySignature.body.connect(forbiddenNotifyNode, 0, legacyNotifyReturn, 0);
        legacyInterface.funcs.push_back(notifySignature);

        BPFunc querySignature; snprintf(querySignature.name, sizeof(querySignature.name), "Query");
        querySignature.ins.clear(); querySignature.outs.clear(); querySignature.ins.push_back(amount);
        BPFuncPin result; snprintf(result.name, sizeof(result.name), "Result"); result.kind = PIN_BOOL;
        querySignature.outs.push_back(result);
        querySignature.body.addNode(BP_FN_ENTRY, 40, 180);
        querySignature.body.addNode(BP_ACT_PRINT, 180, 180);
        querySignature.body.addNode(BP_FN_RETURN, 330, 180); // intentionally disconnected legacy body
        legacyInterface.funcs.push_back(querySignature);

        fs::path interfacePath = tempRoot / "Contract.bpi";
        bool fileOk = writeFile(interfacePath.string(), legacyInterface.serialize());
        BPEditor interfaceEditor; interfaceEditor.projectDir = tempRoot.string();
        bool loadOk = fileOk && interfaceEditor.loadFrom(interfacePath.string(), "Contract.bpi");
        bool memoryShapeOk = loadOk && interfaceEditor.graph.vars.empty() && interfaceEditor.graph.dispatchers.empty() &&
                             interfaceEditor.graph.graphs.size() == 1 && interfaceEditor.graph.main().nodes.empty() &&
                             interfaceEditor.graph.funcs.size() == 2 &&
                             interfaceEditor.graph.funcs[0].body.nodes.size() == 1 &&
                             interfaceEditor.graph.funcs[0].body.nodes[0].def == BP_FN_ENTRY &&
                             interfaceEditor.graph.funcs[0].body.links.empty() &&
                             interfaceEditor.graph.funcs[1].body.nodes.size() == 2 &&
                             interfaceEditor.graph.funcs[1].body.links.size() == 1;
        if (loadOk) {
            interfaceEditor.graph.vars.push_back(forbiddenVariable);
            interfaceEditor.graph.dispatchers.push_back(forbiddenDispatcher);
            interfaceEditor.graph.funcs[0].body.addNode(BP_ACT_PRINT, 200, 200);
            interfaceEditor.graph.funcs[1].body.links.clear();
            interfaceEditor.graph.funcs[1].body.addNode(BP_ACT_PRINT, 200, 200);
        }
        bool saveOk = loadOk && interfaceEditor.saveTo(interfacePath.string());
        BPEditor reloadedInterface; reloadedInterface.projectDir = tempRoot.string();
        bool persistedShapeOk = saveOk && reloadedInterface.loadFrom(interfacePath.string(), "Contract.bpi") &&
                                reloadedInterface.graph.vars.empty() && reloadedInterface.graph.dispatchers.empty() &&
                                reloadedInterface.graph.graphs.size() == 1 && reloadedInterface.graph.main().nodes.empty() &&
                                reloadedInterface.graph.funcs[0].body.nodes.size() == 1 &&
                                reloadedInterface.graph.funcs[1].body.nodes.size() == 2 &&
                                reloadedInterface.graph.funcs[1].body.links.size() == 1;

        BPEditor implementer; implementer.projectDir = tempRoot.string();
        bool implemented = implementer.implementInterfaceAsset("Contract.bpi");
        BPEventDef* notifyEvent = implementer.graph.findEvent("Notify");
        BPFunc* notifyFunction = implementer.graph.findFunc("Notify");
        BPFunc* queryFunction = implementer.graph.findFunc("Query");
        BPNode* notifyNode = nullptr;
        for (BPNode& node : implementer.graph.main().nodes)
            if (node.def == BP_EV_CUSTOM && strcmp(node.sname, "Notify") == 0) { notifyNode = &node; break; }
        bool materializedOk = implemented && notifyEvent && notifyEvent->params.size() == 1 && !notifyFunction &&
                              notifyNode && notifyNode->slit[0] == "Contract.bpi" &&
                              queryFunction && queryFunction->outs.size() == 1;

        BPVarDef received; snprintf(received.name, sizeof(received.name), "Received"); received.type = PIN_NUM;
        implementer.graph.vars.push_back(received);
        int notifyId = notifyNode ? notifyNode->id : 0;
        int setReceived = implementer.graph.main().addNode(BP_VAR_SET, 360, 100);
        snprintf(implementer.graph.main().byId(setReceived)->sname,
                 sizeof(implementer.graph.main().byId(setReceived)->sname), "Received");
        if (notifyId) {
            implementer.graph.main().connect(notifyId, 0, setReceived, 0);
            implementer.graph.main().connect(notifyId, 1, setReceived, 1);
        }
        s.clear();
        int interfaceTarget = s.spawnEmpty("InterfaceEventTarget", {}).id;
        std::string savedProject = g.projectDir, savedBPProject = gBPProjectDir;
        g.projectDir = tempRoot.string(); gBPProjectDir = tempRoot.string(); g.bpScripts.clear();
        App::LiveScript interfaceLive; interfaceLive.entityId = interfaceTarget;
        interfaceLive.inst.graph = &implementer.graph; interfaceLive.inst.entity = s.byId(interfaceTarget); interfaceLive.inst.initVars(nullptr);
        g.bpScripts.push_back(std::move(interfaceLive));
        BPGraph secondImplementer = implementer.graph;
        App::LiveScript secondInterfaceLive; secondInterfaceLive.entityId = interfaceTarget;
        secondInterfaceLive.componentIndex = 1;
        secondInterfaceLive.inst.graph = &secondImplementer;
        secondInterfaceLive.inst.entity = s.byId(interfaceTarget);
        secondInterfaceLive.inst.initVars(nullptr);
        g.bpScripts.push_back(std::move(secondInterfaceLive));
        bpCallInterfaceMessageCb(interfaceTarget, "Contract.bpi", "Notify", { BPValue::N(37) });
        bool runtimeOk = g.bpScripts.size() == 2 &&
                         fabsf(g.bpScripts[0].inst.vars["Received"].single.asNum() - 37.0f) < .001f &&
                         fabsf(g.bpScripts[1].inst.vars["Received"].single.asNum() - 37.0f) < .001f;
        g.bpScripts.clear(); g.projectDir = savedProject; gBPProjectDir = savedBPProject;

        BPGraph implementationRoundtrip;
        bool originRoundtrip = implementationRoundtrip.deserialize(implementer.graph.serialize());
        BPNode* roundtripEvent = nullptr;
        if (originRoundtrip) for (BPNode& node : implementationRoundtrip.main().nodes)
            if (node.def == BP_EV_CUSTOM && strcmp(node.sname, "Notify") == 0) { roundtripEvent = &node; break; }
        originRoundtrip = originRoundtrip && roundtripEvent && roundtripEvent->slit[0] == "Contract.bpi";

        bool ok = memoryShapeOk && persistedShapeOk && materializedOk && runtimeOk && originRoundtrip;
        snprintf(detail, sizeof(detail), "BPI pulita=%s, return protetto=%s, event/function=%s, runtime/origine=%s",
                 memoryShapeOk && persistedShapeOk ? "ok" : "NO", persistedShapeOk ? "ok" : "NO",
                 materializedOk ? "ok" : "NO", runtimeOk && originRoundtrip ? "ok" : "NO");
        report("Blueprint Interface signature-only", ok, detail);
        if (!ok) failures++;
        fs::remove(interfacePath, ec);
        fs::remove(tempRoot, ec);
    }

    {
        // Every object in the Outliner multi-selection gets a scene outline;
        // selectedId remains only the active transform-gizmo owner.
        s.clear();
        int first = s.spawnBox("selection_a", { -2, 0, 0 }, { 1, 1, 1 }, { .6f, .6f, .6f }).id;
        int second = s.spawnBox("selection_b", { 2, 0, 0 }, { 1, 1, 1 }, { .6f, .6f, .6f }).id;
        s.spawnBox("not_selected", { 0, 0, 3 }, { 1, 1, 1 }, { .6f, .6f, .6f });
        g.selectedId = second;
        g.selectedIds = { first, second };
        std::vector<LineVert> selectionLines;
        int highlighted = appendSceneSelectionWires(selectionLines);
        bool ok = highlighted == 2 && selectionLines.size() == 48;
        snprintf(detail, sizeof(detail), "highlight=%d, vertici linee=%d", highlighted, (int)selectionLines.size());
        report("Multi-selection highlight", ok, detail);
        if (!ok) failures++;
        g.selectedId = 0;
        g.selectedIds.clear();
    }

    {
        // Safe Object cast: native components, Blueprint parent inheritance,
        // success/failure exec branches, null output on failure and roundtrip.
        fs::path tempRoot = fs::path(g.baseDir) / "build" / "test_bp_cast";
        std::error_code ec;
        fs::create_directories(tempRoot, ec);
        BPGraph parentClass, childClass;
        childClass.parentAsset = "Parent.bp";
        bool filesOk = writeFile((tempRoot / "Parent.bp").string(), parentClass.serialize()) &&
                       writeFile((tempRoot / "Child.bp").string(), childClass.serialize());

        s.clear();
        int ownerId = s.spawnEmpty("cast_owner", {}).id;
        int cameraId = s.spawnEmpty("cast_camera", {}).id;
        s.byId(cameraId)->isCamera = true;
        int childId = s.spawnEmpty("cast_child", {}).id;
        snprintf(s.byId(childId)->graphPath, sizeof(s.byId(childId)->graphPath), "Child.bp");

        BPGraph castGraph;
        auto addObjectVar = [&](const char* name) {
            BPVarDef var; snprintf(var.name, sizeof(var.name), "%s", name); var.type = PIN_ENT;
            castGraph.vars.push_back(var);
        };
        addObjectVar("nativeResult");
        addObjectVar("blueprintResult");
        addObjectVar("failedResult");
        BPVarDef failedBranch; snprintf(failedBranch.name, sizeof(failedBranch.name), "failedBranch");
        failedBranch.type = PIN_BOOL; castGraph.vars.push_back(failedBranch);

        BPCanvas& cv = castGraph.main();
        int start = cv.addNode(BP_EV_START, 0, 0);
        int findCamera = cv.addNode(BP_FIND, 0, 80);
        snprintf(cv.byId(findCamera)->sname, sizeof(cv.byId(findCamera)->sname), "cast_camera");
        int nativeCast = cv.addNode(BP_CAST_TO_CLASS, 160, 0);
        snprintf(cv.byId(nativeCast)->sname, sizeof(cv.byId(nativeCast)->sname), "component:0");
        int setNative = cv.addNode(BP_VAR_SET, 360, 0);
        snprintf(cv.byId(setNative)->sname, sizeof(cv.byId(setNative)->sname), "nativeResult");
        int findChild = cv.addNode(BP_FIND, 360, 100);
        snprintf(cv.byId(findChild)->sname, sizeof(cv.byId(findChild)->sname), "cast_child");
        int blueprintCast = cv.addNode(BP_CAST_TO_CLASS, 520, 0);
        snprintf(cv.byId(blueprintCast)->sname, sizeof(cv.byId(blueprintCast)->sname), "blueprint:Parent.bp");
        int setBlueprint = cv.addNode(BP_VAR_SET, 720, 0);
        snprintf(cv.byId(setBlueprint)->sname, sizeof(cv.byId(setBlueprint)->sname), "blueprintResult");
        int failedCast = cv.addNode(BP_CAST_TO_CLASS, 880, 0);
        snprintf(cv.byId(failedCast)->sname, sizeof(cv.byId(failedCast)->sname), "component:0");
        int setFailedObject = cv.addNode(BP_VAR_SET, 1080, 100);
        snprintf(cv.byId(setFailedObject)->sname, sizeof(cv.byId(setFailedObject)->sname), "failedResult");
        int setFailedBranch = cv.addNode(BP_VAR_SET, 1240, 100);
        snprintf(cv.byId(setFailedBranch)->sname, sizeof(cv.byId(setFailedBranch)->sname), "failedBranch");
        cv.byId(setFailedBranch)->lit[1].x = 1;

        cv.connect(start, 0, nativeCast, 0); cv.connect(findCamera, 0, nativeCast, 1);
        cv.connect(nativeCast, 0, setNative, 0); cv.connect(nativeCast, 2, setNative, 1);
        cv.connect(setNative, 0, blueprintCast, 0); cv.connect(findChild, 0, blueprintCast, 1);
        cv.connect(blueprintCast, 0, setBlueprint, 0); cv.connect(blueprintCast, 2, setBlueprint, 1);
        cv.connect(setBlueprint, 0, failedCast, 0); cv.connect(findChild, 0, failedCast, 1);
        cv.connect(failedCast, 1, setFailedObject, 0); cv.connect(failedCast, 2, setFailedObject, 1);
        cv.connect(setFailedObject, 0, setFailedBranch, 0);

        std::string serialized = castGraph.serialize();
        BPGraph roundtrip;
        bool roundtripOk = roundtrip.deserialize(serialized) && roundtrip.serialize() == serialized;
        std::string savedProjectDir = gBPProjectDir;
        gBPProjectDir = tempRoot.string();
        BPInstance instance; instance.graph = &roundtrip; instance.entity = s.byId(ownerId); instance.initVars(nullptr);
        BPContext context; context.entity = instance.entity; context.scene = &s;
        instance.fire(BP_EV_START, context);
        gBPProjectDir = savedProjectDir;
        bool runtimeOk = instance.vars["nativeResult"].single.asEnt() == cameraId &&
                         instance.vars["blueprintResult"].single.asEnt() == childId &&
                         instance.vars["failedResult"].single.asEnt() == 0 &&
                         instance.vars["failedBranch"].single.asBool();
        bool shapeOk = bpDefs()[BP_CAST_TO_CLASS].ins[1].kind == PIN_ENT &&
                       bpDefs()[BP_CAST_TO_CLASS].outs[0].kind == PIN_EXEC &&
                       bpDefs()[BP_CAST_TO_CLASS].outs[1].kind == PIN_EXEC &&
                       bpDefs()[BP_CAST_TO_CLASS].outs[2].kind == PIN_ENT;
        bool ok = filesOk && roundtripOk && runtimeOk && shapeOk;
        snprintf(detail, sizeof(detail), "native=%s, Blueprint parent=%s, failed/null=%s, roundtrip=%s",
                 runtimeOk ? "ok" : "NO", runtimeOk ? "ok" : "NO",
                 runtimeOk ? "ok" : "NO", roundtripOk && shapeOk ? "ok" : "NO");
        report("Blueprint Cast To Class", ok, detail);
        if (!ok) failures++;
        fs::remove(tempRoot / "Parent.bp", ec);
        fs::remove(tempRoot / "Child.bp", ec);
        fs::remove(tempRoot, ec);
    }

    {
        // Cross-Blueprint Event Dispatcher: the owner declares and calls it, a
        // listener in a different graph binds one of its own Custom Events, and
        // the argument has to arrive. A mismatched signature must be refused.
        BPGraph ownerGraph, listenerGraph;
        BPDispatcherDef disp; snprintf(disp.name, sizeof(disp.name), "OnScored");
        BPFuncPin amount; snprintf(amount.name, sizeof(amount.name), "Amount"); amount.kind = PIN_NUM;
        disp.params.push_back(amount); ownerGraph.dispatchers.push_back(disp);

        BPEventDef good; snprintf(good.name, sizeof(good.name), "Scored");
        good.params.push_back(amount); listenerGraph.events.push_back(good);
        BPEventDef bad; snprintf(bad.name, sizeof(bad.name), "WrongShape");   // no parameters
        listenerGraph.events.push_back(bad);
        BPCanvas& lc = listenerGraph.main();
        int evNode = lc.addNode(BP_EV_CUSTOM, 0, 0);
        snprintf(lc.byId(evNode)->sname, sizeof(lc.byId(evNode)->sname), "Scored");
        int store = lc.addNode(BP_VAR_SET, 240, 0);
        snprintf(lc.byId(store)->sname, sizeof(lc.byId(store)->sname), "Got");
        BPVarDef got; snprintf(got.name, sizeof(got.name), "Got"); got.type = PIN_NUM;
        listenerGraph.vars.push_back(got);
        lc.connect(evNode, 0, store, 0);
        lc.connect(evNode, 1, store, 1);          // Amount → the value pin

        BPInstance ownerInst; ownerInst.graph = &ownerGraph; ownerInst.initVars(nullptr);
        BPInstance listenerInst; listenerInst.graph = &listenerGraph; listenerInst.initVars(nullptr);

        std::string why;
        bool bound = ownerInst.bindDispatcher("OnScored", 42, 0, "Scored", &listenerGraph, &why);
        bool refusedShape = !ownerInst.bindDispatcher("OnScored", 42, 0, "WrongShape", &listenerGraph, &why);
        bool refusedMissing = !ownerInst.bindDispatcher("NoSuchDispatcher", 42, 0, "Scored", &listenerGraph, &why);

        // fire the dispatcher: the owner must route the event to the listener
        static BPInstance* routedTo = nullptr; routedTo = &listenerInst;
        static float delivered = -1; delivered = -1;
        BPContext ownerCtx = makeBPCtx(nullptr, 0, -1, 0);
        ownerCtx.fireDispatcherEvent = [](int, int, const char* evName, const std::vector<BPValue>& args) {
            BPContext inner = makeBPCtx(nullptr, 0, -1, 0);
            routedTo->fireCustomWithArgs(evName, args, inner);
            delivered = args.empty() ? -1 : args[0].asNum();
        };
        BPCanvas& oc = ownerGraph.main();
        int callNode = oc.addNode(BP_CALL_DISPATCH, 0, 0);
        snprintf(oc.byId(callNode)->sname, sizeof(oc.byId(callNode)->sname), "OnScored");
        oc.byId(callNode)->lit[1] = { 7, 0, 0 };
        int startNode = oc.addNode(BP_EV_START, -240, 0);
        oc.connect(startNode, 0, callNode, 0);
        ownerInst.fire(BP_EV_START, ownerCtx);

        bool argOk = fabsf(delivered - 7.0f) < 0.0001f;
        bool storedOk = fabsf(listenerInst.vars["Got"].single.asNum() - 7.0f) < 0.0001f;

        // An actor with several Blueprint components: the dispatcher lives on the
        // SECOND one (a Health Component, say). Looking only at the first made the
        // host report "the target owns no dispatcher with that name".
        BPGraph plainComponent, healthComponent;          // first has nothing, second owns it
        healthComponent.dispatchers.push_back(disp);
        g.bpScripts.clear();
        g.bpScripts.push_back({ 900, 0, {} }); g.bpScripts.back().inst.graph = &plainComponent;
        g.bpScripts.back().inst.initVars(nullptr);
        g.bpScripts.push_back({ 900, 1, {} }); g.bpScripts.back().inst.graph = &healthComponent;
        g.bpScripts.back().inst.initVars(nullptr);
        g.bpScripts.push_back({ 901, 0, {} }); g.bpScripts.back().inst.graph = &listenerGraph;
        g.bpScripts.back().inst.initVars(nullptr);
        bool multiComponent = bpBindDispatcherCb(900, 0, "OnScored", 901, 0, "Scored");
        bool wrongNameStillFails = !bpBindDispatcherCb(900, 0, "Nope", 901, 0, "Scored");
        g.bpScripts.clear();

        // The editor must refuse a delegate wire whose Custom Event does not match
        // the Dispatcher — that mismatch used to be discoverable only at runtime.
        BPEditor linkCheck;
        linkCheck.graph = ownerGraph;                       // owns OnScored(Amount:Float)
        BPEventDef okEvent; snprintf(okEvent.name, sizeof(okEvent.name), "Good");
        okEvent.params.push_back(amount); linkCheck.graph.events.push_back(okEvent);
        BPEventDef badEvent; snprintf(badEvent.name, sizeof(badEvent.name), "Bad");
        linkCheck.graph.events.push_back(badEvent);         // no parameters
        BPCanvas& lk = linkCheck.graph.main();
        int bindId = lk.addNode(BP_BIND_EVENT, 0, 0);
        snprintf(lk.byId(bindId)->sname, sizeof(lk.byId(bindId)->sname), "OnScored");
        int okId = lk.addNode(BP_CREATE_EVENT, -200, 0);
        snprintf(lk.byId(okId)->sname, sizeof(lk.byId(okId)->sname), "Good");
        int badId = lk.addNode(BP_CREATE_EVENT, -200, 120);
        snprintf(lk.byId(badId)->sname, sizeof(lk.byId(badId)->sname), "Bad");
        std::string whyLink;
        bool matchAccepted = linkCheck.delegateLinkAllowed(lk, okId, 0, bindId, 1, whyLink);
        bool mismatchRefused = !linkCheck.delegateLinkAllowed(lk, badId, 0, bindId, 1, whyLink);
        // a non-delegate link must not be touched by the rule
        bool otherLinksFree = linkCheck.delegateLinkAllowed(lk, okId, 0, bindId, 0, whyLink);
        bool linkRuleOk = matchAccepted && mismatchRefused && otherLinksFree;

        bool ok = bound && refusedShape && refusedMissing && argOk && storedOk &&
                  multiComponent && wrongNameStillFails && linkRuleOk;
        snprintf(detail, sizeof(detail),
                 "bind=%s, sig refused=%s, unknown refused=%s, arg=%.7g, listener var=%s, "
                 "2nd component=%s, bad name refused=%s, wire rule=%s",
                 bound ? "ok" : "NO", refusedShape ? "ok" : "NO", refusedMissing ? "ok" : "NO",
                 delivered, storedOk ? "ok" : "NO",
                 multiComponent ? "ok" : "NO", wrongNameStillFails ? "ok" : "NO",
                 linkRuleOk ? "ok" : "NO");
        report("Blueprint Dispatcher cross-graph", ok, detail);
        if (!ok) failures++;
    }

    {
        // Calling a member on an external Widget held in a variable: the class
        // is "widget:<rel.wgt>", the Target pin is the instance handle, and the
        // call has to reach the widget callbacks rather than the Blueprint ones.
        fs::path tempRoot = fs::path(g.baseDir) / "build" / "test_widget_member";
        std::error_code ec; fs::remove_all(tempRoot, ec); ec.clear();
        fs::create_directories(tempRoot, ec);

        WidgetAsset designer;                       // a .wgt is designer + marker + graph
        BPGraph widgetGraph;
        BPFunc addScore; snprintf(addScore.name, sizeof(addScore.name), "AddScore");
        addScore.scope = VS_PUBLIC; addScore.ins.clear(); addScore.outs.clear();
        BPFuncPin amountPin; snprintf(amountPin.name, sizeof(amountPin.name), "Amount"); amountPin.kind = PIN_NUM;
        BPFuncPin totalPin; snprintf(totalPin.name, sizeof(totalPin.name), "Total"); totalPin.kind = PIN_NUM;
        addScore.ins.push_back(amountPin); addScore.outs.push_back(totalPin);
        widgetGraph.funcs.push_back(addScore);
        BPVarDef hidden; snprintf(hidden.name, sizeof(hidden.name), "Score");
        hidden.type = PIN_NUM; hidden.scope = VS_PUBLIC; widgetGraph.vars.push_back(hidden);
        writeFile((tempRoot / "Hud.wgt").string(),
                  designer.serialize() + WIDGET_GRAPH_MARKER + widgetGraph.serialize());

        std::string savedProjectDir = gBPProjectDir; gBPProjectDir = tempRoot.string();
        BPGraph loaded;
        bool classDetected = bpMemberClassIsWidget("widget:Hud.wgt") && !bpMemberClassIsWidget("blueprint:X.bp");
        bool graphLoaded = bpLoadWidgetGraph(tempRoot.string(), "widget:Hud.wgt", loaded);
        bool memberFound = graphLoaded && loaded.findFunc("AddScore") != nullptr;

        // a caller graph whose node targets the widget by handle
        BPGraph caller;
        BPCanvas& cc = caller.main();
        int startId = cc.addNode(BP_EV_START, 0, 0);
        int callId = cc.addNode(BP_MEMBER_ACCESS, 240, 0);
        BPNode* callNode = cc.byId(callId);
        callNode->choice = 2;                                    // impure function call
        callNode->slit[0] = "widget:Hud.wgt";
        snprintf(callNode->sname, sizeof(callNode->sname), "AddScore");
        callNode->lit[1] = { 77, 0, 0 };                         // Target handle literal
        callNode->lit[2] = { 5, 0, 0 };                          // Amount
        cc.connect(startId, 0, callId, 0);

        static int seenHandle = 0; seenHandle = 0;
        static std::string seenFn; seenFn.clear();
        static float seenArg = 0; seenArg = 0;
        static bool wentToBlueprint = false; wentToBlueprint = false;
        BPInstance callerInst; callerInst.graph = &caller; callerInst.initVars(nullptr);
        BPContext callCtx = makeBPCtx(nullptr, 0, -1, 0);
        callCtx.callWidgetMember = [](int handle, const char* fn, const std::vector<BPValue>& args) {
            seenHandle = handle; seenFn = fn ? fn : "";
            seenArg = args.empty() ? 0 : args[0].asNum();
            return std::vector<BPValue>{ BPValue::N(seenArg * 2) };
        };
        callCtx.callBlueprintMember = [](int, const char*, const char*, const std::vector<BPValue>&) {
            wentToBlueprint = true; return std::vector<BPValue>{};
        };
        callerInst.fire(BP_EV_START, callCtx);
        gBPProjectDir = savedProjectDir;

        bool routed = seenHandle == 77 && seenFn == "AddScore" &&
                      fabsf(seenArg - 5.0f) < 0.0001f && !wentToBlueprint;
        bool ok = classDetected && graphLoaded && memberFound && routed;
        snprintf(detail, sizeof(detail),
                 "class=%s, .wgt graph=%s, member=%s, handle=%d, fn=%s, arg=%.7g, blueprint path avoided=%s",
                 classDetected ? "ok" : "NO", graphLoaded ? "ok" : "NO", memberFound ? "ok" : "NO",
                 seenHandle, seenFn.empty() ? "-" : seenFn.c_str(), seenArg, wentToBlueprint ? "NO" : "ok");
        // A widget reference has to answer with its class from every place an
        // object reference does, or dragging off it offers no members at all.
        BPGraph refGraph;
        BPVarDef hudVar; snprintf(hudVar.name, sizeof(hudVar.name), "Hud");
        hudVar.type = PIN_WIDGET; snprintf(hudVar.refClass, sizeof(hudVar.refClass), "widget:Hud.wgt");
        refGraph.vars.push_back(hudVar);
        BPCanvas& rc = refGraph.main();
        int getId = rc.addNode(BP_VAR_GET, 0, 0);
        snprintf(rc.byId(getId)->sname, sizeof(rc.byId(getId)->sname), "Hud");
        int setId = rc.addNode(BP_VAR_SET, 200, 0);
        snprintf(rc.byId(setId)->sname, sizeof(rc.byId(setId)->sname), "Hud");
        int createId = rc.addNode(BP_CREATE_WIDGET, 400, 0);
        snprintf(rc.byId(createId)->sname, sizeof(rc.byId(createId)->sname), "Hud.wgt");
        int rerouteId = rc.addNode(BP_REROUTE, 600, 0);
        rc.connect(getId, 0, rerouteId, 0);
        bool getClass = bpPinRefClass(rc, refGraph, getId, 0, true) == "widget:Hud.wgt";
        bool setClass = bpPinRefClass(rc, refGraph, setId, 1, true) == "widget:Hud.wgt";
        bool createClass = bpPinRefClass(rc, refGraph, createId, 1, true) == "widget:Hud.wgt";
        bool rerouteClass = bpPinRefClass(rc, refGraph, rerouteId, 0, true) == "widget:Hud.wgt";
        bool refsOk = getClass && setClass && createClass && rerouteClass;
        // it has to behave like an object reference, not like the int it wraps:
        // its own kind, no numeric cross-wiring, and a member Target that takes it
        bool getKind = bpEffKind(rc, refGraph, getId, 0, true, 0) == PIN_WIDGET;
        BPNode probe; probe.def = BP_MEMBER_ACCESS; probe.choice = 2; probe.slit[0] = "widget:Hud.wgt";
        BPNodeDef probeDef = bpNodeDefForTest(tempRoot.string(), probe);
        bool targetIsWidget = probeDef.nIns > 1 && probeDef.ins[1].kind == PIN_WIDGET;
        bool notAnInt = !bpPinKindsCompatible(PIN_WIDGET, PIN_NUM) &&
                        !bpPinKindsCompatible(PIN_WIDGET, PIN_INT) &&
                        bpPinKindsCompatible(PIN_WIDGET, PIN_WIDGET);
        bool objectLike = getKind && targetIsWidget && notAnInt;

        ok = ok && refsOk && objectLike;
        snprintf(detail, sizeof(detail),
                 "class=%s, .wgt graph=%s, member=%s, handle=%d, fn=%s, arg=%.7g, blueprint path avoided=%s, "
                 "refs=%s%s%s%s object-like=%s",
                 classDetected ? "ok" : "NO", graphLoaded ? "ok" : "NO", memberFound ? "ok" : "NO",
                 seenHandle, seenFn.empty() ? "-" : seenFn.c_str(), seenArg, wentToBlueprint ? "NO" : "ok",
                 getClass ? "ok " : "NO ", setClass ? "ok " : "NO ",
                 createClass ? "ok " : "NO ", rerouteClass ? "ok" : "NO",
                 objectLike ? "ok" : "NO");
        report("Widget member through a variable", ok, detail);
        if (!ok) failures++;
        fs::remove_all(tempRoot, ec);
    }

    {
        // Progress Bar / Slider range: the fill is where the value sits between
        // Min and Max, so raw health no longer pins the bar to full.
        WidgetNode bar; bar.type = WT_PROGRESSBAR;
        bool defaultsAsPercent = true;
        widgetSetNumber(bar, "Percent", 0.25f);
        defaultsAsPercent &= fabsf(widgetFillFraction(bar) - 0.25f) < 0.0001f;
        widgetSetNumber(bar, "Percent", 5.0f);                    // above the 0..1 default
        defaultsAsPercent &= fabsf(widgetFillFraction(bar) - 1.0f) < 0.0001f;

        widgetSetNumber(bar, "Min", 0.0f);
        widgetSetNumber(bar, "Max", 100.0f);
        widgetSetNumber(bar, "Percent", 75.0f);
        bool rangeWorks = fabsf(widgetFillFraction(bar) - 0.75f) < 0.0001f;
        float readBack = 0;
        bool getsBack = widgetGetNumber(bar, "Percent", readBack) && fabsf(readBack - 75.0f) < 0.0001f;
        float mn = 0, mx = 0;
        getsBack &= widgetGetNumber(bar, "Min", mn) && widgetGetNumber(bar, "Max", mx) &&
                    mn == 0.0f && fabsf(mx - 100.0f) < 0.0001f;
        widgetSetNumber(bar, "Percent", 250.0f);                  // clamped to Max
        bool clampsToRange = fabsf(bar.value - 100.0f) < 0.0001f;

        // an offset range, and a degenerate one that must not divide by zero
        widgetSetNumber(bar, "Min", 20.0f); widgetSetNumber(bar, "Max", 40.0f);
        widgetSetNumber(bar, "Percent", 30.0f);
        bool offsetRange = fabsf(widgetFillFraction(bar) - 0.5f) < 0.0001f;
        WidgetNode flat; flat.type = WT_PROGRESSBAR; flat.minValue = 5; flat.maxValue = 5; flat.value = 5;
        bool degenerateSafe = widgetFillFraction(flat) == 0.0f;

        // survives a save/load round trip
        WidgetAsset asset;
        int id = asset.addNode(WT_PROGRESSBAR, 0);
        if (WidgetNode* n = asset.find(id)) { n->minValue = -50; n->maxValue = 50; n->value = 0; }
        WidgetAsset reloaded;
        bool roundTrip = reloaded.deserialize(asset.serialize());
        if (roundTrip) {
            const WidgetNode* n = reloaded.find(id);
            roundTrip = n && n->minValue == -50.0f && n->maxValue == 50.0f &&
                        fabsf(widgetFillFraction(*n) - 0.5f) < 0.0001f;
        }

        bool ok = defaultsAsPercent && rangeWorks && getsBack && clampsToRange &&
                  offsetRange && degenerateSafe && roundTrip;
        snprintf(detail, sizeof(detail),
                 "0..1 default=%s, 0..100 at 75=%s, get back=%s, clamp=%s, 20..40 at 30=%s, "
                 "empty span=%s, roundtrip=%s",
                 defaultsAsPercent ? "ok" : "NO", rangeWorks ? "ok" : "NO", getsBack ? "ok" : "NO",
                 clampsToRange ? "ok" : "NO", offsetRange ? "ok" : "NO",
                 degenerateSafe ? "ok" : "NO", roundTrip ? "ok" : "NO");
        report("Widget bar Min/Max range", ok, detail);
        if (!ok) failures++;
    }

    {
        // Every direct Get node must read back exactly what its component holds,
        // through the same callbacks the runtime uses.
        static WidgetAsset probe;                       // one element named "Bar"
        probe = WidgetAsset{};
        int barId = probe.addNode(WT_PROGRESSBAR, 0);
        WidgetNode* bn = probe.find(barId);
        snprintf(bn->name, sizeof(bn->name), "Bar");
        bn->minValue = 10; bn->maxValue = 60; bn->value = 35;
        bn->hAlign = WA_END; bn->vAlign = WA_CENTER; bn->anchor = WANCH_BOT_RIGHT;
        bn->pivotX = 0.25f; bn->pivotY = 0.75f;
        bn->visible = false; bn->enabled = false; bn->renderOpacity = 0.5f;
        bn->w = 320; bn->h = 24; bn->x = 12; bn->y = 34;
        bn->color = { 0.2f, 0.4f, 0.6f }; bn->alpha = 0.8f;
        snprintf(bn->text, sizeof(bn->text), "HP");

        BPGraph readerGraph;
        BPCanvas& rgc = readerGraph.main();
        BPInstance reader; reader.graph = &readerGraph; reader.initVars(nullptr);
        BPContext rctx = makeBPCtx(nullptr, 0, -1, 0);
        // route the widget callbacks at the probe asset, by element name
        auto element = [](const char* name) -> WidgetNode* {
            for (WidgetNode& node : probe.nodes) if (_stricmp(node.name, name) == 0) return &node;
            return nullptr;
        };
        static auto elem = element;
        rctx.getWidgetNumber = [](int, const char* el, const char* prop, float& out) {
            WidgetNode* n2 = elem(el); return n2 && widgetGetNumber(*n2, prop, out); };
        rctx.getWidgetBool = [](int, const char* el, const char* prop, bool& out) {
            WidgetNode* n2 = elem(el); return n2 && widgetGetBool(*n2, prop, out); };
        rctx.getWidgetString = [](int, const char* el, const char* prop, std::string& out) {
            WidgetNode* n2 = elem(el); return n2 && widgetGetString(*n2, prop, out); };
        rctx.getWidgetColor = [](int, const char* el, const char* prop, Vec3& rgb, float& a) {
            WidgetNode* n2 = elem(el); return n2 && widgetGetColor(*n2, prop, rgb, a); };

        auto readNode = [&](int def, int outPin) {
            int id = rgc.addNode(def, 0, 0);
            rgc.byId(id)->slit[1] = "Bar";               // Element literal
            return reader.evalOutForTest(rgc, *rgc.byId(id), outPin, rctx);
        };
        bool okPercent = fabsf(readNode(BP_GET_WIDGET_PERCENT, 0).asNum() - 35.0f) < 0.0001f;
        bool okMin = fabsf(readNode(BP_GET_WIDGET_RANGE, 0).asNum() - 10.0f) < 0.0001f;
        bool okMax = fabsf(readNode(BP_GET_WIDGET_RANGE, 1).asNum() - 60.0f) < 0.0001f;
        bool okHAlign = (int)readNode(BP_GET_WIDGET_HALIGN, 0).asNum() == WA_END;
        bool okVAlign = (int)readNode(BP_GET_WIDGET_VALIGN, 0).asNum() == WA_CENTER;
        bool okAnchor = (int)readNode(BP_GET_WIDGET_ANCHOR, 0).asNum() == WANCH_BOT_RIGHT;
        Vec3 piv = readNode(BP_GET_WIDGET_PIVOT, 0).asVec();
        bool okPivot = fabsf(piv.x - 0.25f) < 0.0001f && fabsf(piv.y - 0.75f) < 0.0001f;
        bool okText = readNode(BP_GET_WIDGET_TEXT, 0).str == "HP";
        bool okVisible = readNode(BP_GET_WIDGET_VISIBLE, 0).asBool() == false;
        bool okEnabled = readNode(BP_GET_WIDGET_ENABLED, 0).asBool() == false;
        bool okOpacity = fabsf(readNode(BP_GET_WIDGET_OPACITY, 0).asNum() - 0.5f) < 0.0001f;
        bool okSize = fabsf(readNode(BP_GET_WIDGET_SIZE, 0).asNum() - 320.0f) < 0.0001f &&
                      fabsf(readNode(BP_GET_WIDGET_SIZE, 1).asNum() - 24.0f) < 0.0001f;
        bool okPos = fabsf(readNode(BP_GET_WIDGET_POSITION, 0).asNum() - 12.0f) < 0.0001f &&
                     fabsf(readNode(BP_GET_WIDGET_POSITION, 1).asNum() - 34.0f) < 0.0001f;
        BPValue col = readNode(BP_GET_WIDGET_COLOR_DIRECT, 0);
        bool okColor = fabsf(col.asVec().y - 0.4f) < 0.0001f && fabsf(col.alpha - 0.8f) < 0.0001f;

        bool ok = okPercent && okMin && okMax && okHAlign && okVAlign && okAnchor && okPivot &&
                  okText && okVisible && okEnabled && okOpacity && okSize && okPos && okColor;
        snprintf(detail, sizeof(detail),
                 "percent=%s range=%s/%s align=%s/%s anchor=%s pivot=%s text=%s visible=%s "
                 "enabled=%s opacity=%s size=%s pos=%s color=%s",
                 okPercent?"ok":"NO", okMin?"ok":"NO", okMax?"ok":"NO", okHAlign?"ok":"NO",
                 okVAlign?"ok":"NO", okAnchor?"ok":"NO", okPivot?"ok":"NO", okText?"ok":"NO",
                 okVisible?"ok":"NO", okEnabled?"ok":"NO", okOpacity?"ok":"NO", okSize?"ok":"NO",
                 okPos?"ok":"NO", okColor?"ok":"NO");
        report("Widget direct Get nodes", ok, detail);
        if (!ok) failures++;
    }

    {
        // Context-sensitive Blueprint members plus independent access/Inspector metadata.
        fs::path tempRoot = fs::path(g.baseDir) / "build" / "test_bp_members";
        std::error_code ec; fs::remove_all(tempRoot, ec); ec.clear(); fs::create_directories(tempRoot / "Components", ec);
        BPGraph owner;
        BPVarDef health; snprintf(health.name, sizeof(health.name), "Health"); health.type = PIN_NUM;
        health.scope = VS_PUBLIC; health.expose = false; owner.vars.push_back(health);
        BPVarDef secret; snprintf(secret.name, sizeof(secret.name), "Secret"); secret.type = PIN_NUM;
        secret.scope = VS_PRIVATE; secret.expose = true; owner.vars.push_back(secret);
        BPFunc ping; snprintf(ping.name, sizeof(ping.name), "Ping"); ping.ins.clear(); ping.outs.clear();
        ping.scope = VS_PUBLIC;
        BPFuncPin x, y; snprintf(x.name, sizeof(x.name), "X"); x.kind = PIN_NUM;
        snprintf(y.name, sizeof(y.name), "Y"); y.kind = PIN_NUM; ping.ins.push_back(x); ping.outs.push_back(y);
        owner.funcs.push_back(ping);
        BPEventDef alert; snprintf(alert.name, sizeof(alert.name), "Alert"); alert.params.push_back(x); owner.events.push_back(alert);
        BPEventDef hiddenAlert; snprintf(hiddenAlert.name, sizeof(hiddenAlert.name), "HiddenAlert");
        hiddenAlert.scope = VS_PRIVATE; owner.events.push_back(hiddenAlert);
        bool fileOk = writeFile((tempRoot / "Components" / "Member.bp").string(), owner.serialize());
        std::string repairedMemberPath;
        bool movedAssetOk = bpResolveBlueprintAssetPath(tempRoot.string(), "Legacy\\Member.bp", repairedMemberPath) &&
                            repairedMemberPath == "Components\\Member.bp";
        std::string savedAppProjectDir = g.projectDir;
        g.projectDir = tempRoot.string();
        bool runtimeClassPathOk = appBlueprintPathIsA("Legacy\\Member.bp", "Components\\Member.bp");
        g.projectDir = savedAppProjectDir;

        BPGraph ownerRoundtrip; bool metaOk = ownerRoundtrip.deserialize(owner.serialize());
        BPVarDef* roundSecret = ownerRoundtrip.findVar("Secret"); BPFunc* roundPing = ownerRoundtrip.findFunc("Ping");
        BPEventDef* roundAlert = ownerRoundtrip.findEvent("Alert");
        BPEventDef* roundHiddenAlert = ownerRoundtrip.findEvent("HiddenAlert");
        metaOk = metaOk && roundSecret && roundSecret->scope == VS_PRIVATE && roundSecret->expose &&
                 roundPing && roundPing->scope == VS_PUBLIC &&
                 roundAlert && roundAlert->scope == VS_PUBLIC &&
                 roundHiddenAlert && roundHiddenAlert->scope == VS_PRIVATE;
        std::map<std::string, Vec3> privateOverride; privateOverride["Secret"] = { 4.25f, 0, 0 };
        BPInstance exposedPrivate; exposedPrivate.graph = &ownerRoundtrip; exposedPrivate.initVars(&privateOverride);
        bool inspectorIndependent = fabsf(exposedPrivate.vars["Secret"].single.asNum() - 4.25f) < .001f;

        // The node clipboard is shared across Blueprint editors and transports
        // Custom Event signatures. Name collisions create a local copy instead
        // of hijacking an existing event in the destination asset.
        BPCanvas clipSource;
        int copiedEvent = clipSource.addNode(BP_EV_CUSTOM, 10, 20);
        int copiedCall = clipSource.addNode(BP_CALL_EVENT, 230, 20);
        snprintf(clipSource.byId(copiedEvent)->sname, sizeof(clipSource.byId(copiedEvent)->sname), "Alert");
        snprintf(clipSource.byId(copiedCall)->sname, sizeof(clipSource.byId(copiedCall)->sname), "Alert");
        clipSource.connect(copiedEvent, 0, copiedCall, 0);
        bpCopyNodesToClipboard(ownerRoundtrip, clipSource, { copiedEvent, copiedCall });
        BPGraph clipboardTarget;
        BPEventDef occupied; snprintf(occupied.name, sizeof(occupied.name), "Alert"); clipboardTarget.events.push_back(occupied);
        BPCanvas& clipboardCanvas = clipboardTarget.main();
        std::vector<int> pastedNodes = bpPasteNodesFromClipboard(clipboardTarget, clipboardCanvas, 500, 300);
        // The pasted definition must take a fresh unique name (bpUniqueMemberName
        // gives "Alert" -> "Alert2") rather than hijack the event already defined
        // here. Assert the behaviour, not the exact suffix convention.
        std::string pastedName;
        for (int pastedId : pastedNodes)
            if (BPNode* pasted = clipboardCanvas.byId(pastedId)) { pastedName = pasted->sname; break; }
        BPEventDef* pastedSignature = pastedName.empty() ? nullptr : clipboardTarget.findEvent(pastedName.c_str());
        BPEventDef* localSignature = clipboardTarget.findEvent("Alert");
        bool clipboardOk = pastedNodes.size() == 2 && pastedSignature && localSignature &&
                           _stricmp(pastedName.c_str(), "Alert") != 0 &&   // never hijacks the local event
                           localSignature->params.empty() &&               // the local one stays untouched
                           pastedSignature->scope == VS_PUBLIC &&
                           pastedSignature->params.size() == 1 &&          // signature travelled with the copy
                           clipboardCanvas.links.size() == 1;
        for (int pastedId : pastedNodes) {                                 // both nodes remapped to the copy
            BPNode* pasted = clipboardCanvas.byId(pastedId);
            clipboardOk = clipboardOk && pasted && pastedName == pasted->sname;
        }

        BPGraph caller;
        BPVarDef observed; snprintf(observed.name, sizeof(observed.name), "Observed"); observed.type = PIN_NUM; caller.vars.push_back(observed);
        BPVarDef returned; snprintf(returned.name, sizeof(returned.name), "Returned"); returned.type = PIN_NUM; caller.vars.push_back(returned);
        BPCanvas& cv = caller.main();
        int start = cv.addNode(BP_EV_START, 0, 0), self = cv.addNode(BP_SELF, 0, 80);
        int value = cv.addNode(BP_VAL_NUM, 0, 150); cv.byId(value)->prop = .1f;
        auto memberNode = [&](int choice, const char* name, float px) {
            int id = cv.addNode(BP_MEMBER_ACCESS, px, 0); BPNode* node = cv.byId(id);
            node->choice = choice; snprintf(node->sname, sizeof(node->sname), "%s", name); node->slit[0] = "Legacy\\Member.bp"; return id;
        };
        int memberSet = memberNode(1, "Health", 180), memberGet = memberNode(0, "Health", 360);
        int setObserved = cv.addNode(BP_VAR_SET, 520, 0); snprintf(cv.byId(setObserved)->sname, sizeof(cv.byId(setObserved)->sname), "Observed");
        int memberCall = memberNode(2, "Ping", 700);
        int setReturned = cv.addNode(BP_VAR_SET, 900, 0); snprintf(cv.byId(setReturned)->sname, sizeof(cv.byId(setReturned)->sname), "Returned");
        int memberEvent = memberNode(4, "Alert", 1080);
        cv.connect(start,0,memberSet,0); cv.connect(self,0,memberSet,1); cv.connect(value,0,memberSet,2);
        cv.connect(memberSet,0,setObserved,0); cv.connect(self,0,memberGet,0); cv.connect(memberGet,0,setObserved,1);
        cv.connect(setObserved,0,memberCall,0); cv.connect(self,0,memberCall,1); cv.connect(value,0,memberCall,2);
        cv.connect(memberCall,0,setReturned,0); cv.connect(memberCall,1,setReturned,1);
        cv.connect(setReturned,0,memberEvent,0); cv.connect(self,0,memberEvent,1); cv.connect(value,0,memberEvent,2);
        BPGraph callerRoundtrip; bool nodeRoundtrip = callerRoundtrip.deserialize(caller.serialize()) && callerRoundtrip.serialize() == caller.serialize();

        static float memberTestValue = 0, memberTestEvent = 0;
        memberTestValue = memberTestEvent = 0;
        s.clear(); int ownerId = s.spawnEmpty("member_caller", {}).id;
        BPInstance instance; instance.graph = &callerRoundtrip; instance.entity = s.byId(ownerId); instance.initVars(nullptr);
        BPContext context; context.entity = instance.entity; context.scene = &s;
        context.setBlueprintMember = [](int,const char*,const char*,const BPValue& v){memberTestValue=v.asNum();return true;};
        context.getBlueprintMember = [](int,const char*,const char*){return BPValue::N(memberTestValue);};
        context.callBlueprintMember = [](int,const char*,const char*,const std::vector<BPValue>& args){
            return std::vector<BPValue>{BPValue::N(args.empty()?0:args[0].asNum()+1)};};
        context.fireBlueprintMemberEvent = [](int,const char*,const char*,const std::vector<BPValue>& args){
            memberTestEvent=args.empty()?0:args[0].asNum();};
        std::string savedProjectDir = gBPProjectDir; gBPProjectDir = tempRoot.string();
        instance.fire(BP_EV_START, context); gBPProjectDir = savedProjectDir;
        bool runtimeOk = fabsf(memberTestValue-.1f)<.0001f && fabsf(memberTestEvent-.1f)<.0001f &&
                         fabsf(instance.vars["Observed"].single.asNum()-.1f)<.0001f &&
                         fabsf(instance.vars["Returned"].single.asNum()-1.1f)<.0001f;
        bool ok = fileOk && movedAssetOk && runtimeClassPathOk && metaOk && inspectorIndependent && clipboardOk && nodeRoundtrip && runtimeOk;
        snprintf(detail,sizeof(detail),"metadata=%s, asset spostato=%s, classe runtime=%s, private Inspector=%s, clipboard cross-BP=%s (%s), member runtime=%s, float=%.7g",
                 metaOk?"ok":"NO",movedAssetOk?"ok":"NO",runtimeClassPathOk?"ok":"NO",inspectorIndependent?"ok":"NO",clipboardOk?"ok":"NO",
                 pastedName.empty()?"-":pastedName.c_str(),
                 nodeRoundtrip&&runtimeOk?"ok":"NO",memberTestValue);
        report("Context Sensitive / Blueprint members",ok,detail);if(!ok)failures++;
        fs::remove_all(tempRoot, ec);
    }
    {
        // Frustum culling must never reject geometry the camera can actually see.
        // Ground truth is independent of the plane test: sample points across each
        // object's world AABB and project them to raw clip space (keeping w, so
        // geometry behind the eye is not folded back into the cube by the divide).
        OrbitCamera cam;
        cam.target = { 0, 0, 0 }; cam.distance = 12; cam.yaw = 0.6f; cam.pitch = 0.3f;
        cam.update(16.0f / 9.0f);
        Frustum fr = Frustum::fromMatrix(cam.projView);
        const Mat4& M = cam.projView;
        int total = 0, culled = 0, falseCull = 0;
        for (int ix = -6; ix <= 6; ix++)
        for (int iy = -3; iy <= 3; iy++)
        for (int iz = -6; iz <= 6; iz++) {
            DrawItem item;
            item.mesh = (MeshType)(((ix + iy + iz) % MESH_COUNT + MESH_COUNT) % MESH_COUNT);
            item.model = Mat4::compose({ ix * 4.0f, iy * 4.0f, iz * 4.0f },
                                       Quat::fromEulerDeg((float)(ix * 11), (float)(iy * 23), (float)(iz * 7)),
                                       { 1.0f + (ix + 6) * 0.15f, 1.0f, 2.0f });   // non-uniform + rotated
            Vec3 c, h;
            drawItemBounds(item, c, h);
            bool kept = fr.intersectsAABB(c, h);
            total++;
            if (kept) continue;
            culled++;
            const int N = 5;   // 6^3 samples spanning the box
            for (int a = 0; a <= N; a++)
            for (int b = 0; b <= N; b++)
            for (int d = 0; d <= N; d++) {
                Vec3 p = { c.x + h.x * (2.0f * a / N - 1),
                           c.y + h.y * (2.0f * b / N - 1),
                           c.z + h.z * (2.0f * d / N - 1) };
                float qx = M[0]*p.x + M[4]*p.y + M[8]*p.z  + M[12];
                float qy = M[1]*p.x + M[5]*p.y + M[9]*p.z  + M[13];
                float qz = M[2]*p.x + M[6]*p.y + M[10]*p.z + M[14];
                float qw = M[3]*p.x + M[7]*p.y + M[11]*p.z + M[15];
                if (qw > 0 && qx >= -qw && qx <= qw && qy >= -qw && qy <= qw && qz >= -qw && qz <= qw) {
                    falseCull++;
                    a = b = d = N + 1;   // this object is already proven visible
                }
            }
        }
        // a degenerate "keep everything" implementation must not pass either
        bool pass = falseCull == 0 && culled > total / 4;
        snprintf(detail, sizeof(detail), "%d objects, %d culled, %d visible wrongly discarded",
                 total, culled, falseCull);
        report("Frustum culling camera", pass, detail);
        if (!pass) failures++;
    }
    {
        // Reordering blueprint components must carry each component's overrides
        // with it, including across the legacy slot-0 storage (graphPath).
        s.clear();
        Entity& owner = s.spawnBox("owner", { 0, 1, 0 }, { 1, 1, 1 }, {}, BodyType::Static);
        snprintf(owner.graphPath, sizeof(owner.graphPath), "A.bp");
        owner.varOverrides["v"] = { 1, 0, 0 };
        BlueprintComponentDef b; b.graphPath = "B.bp"; b.varOverrides["v"] = { 2, 0, 0 };
        BlueprintComponentDef c; c.graphPath = "C.bp"; c.varOverrides["v"] = { 3, 0, 0 };
        owner.additionalBlueprints = { b, c };
        auto layout = [&]() {
            std::string out;
            for (int i = 0; i < entityBlueprintCount(owner); i++) {
                auto* ov = entityBlueprintOverrides(owner, i);
                char cell[16];
                snprintf(cell, sizeof(cell), "%s%.0f ", entityBlueprintPath(owner, i), ov ? (*ov)["v"].x : -1);
                out += cell;
            }
            return out;
        };
        std::string start = layout();
        moveEntityBlueprint(owner, 0, 2);          // A between B and C
        std::string afterFirst = layout();
        moveEntityBlueprint(owner, 2, 0);          // C to the front
        std::string afterSecond = layout();
        bool ok = start == "A.bp1 B.bp2 C.bp3 " &&
                  afterFirst == "B.bp2 A.bp1 C.bp3 " &&
                  afterSecond == "C.bp3 B.bp2 A.bp1 " &&
                  owner.additionalBlueprints.size() == 2 &&
                  _stricmp(owner.graphPath, "C.bp") == 0;
        snprintf(detail, sizeof(detail), "[%s] -> [%s] -> [%s]",
                 start.c_str(), afterFirst.c_str(), afterSecond.c_str());
        report("Riordino componenti Blueprint", ok, detail);
        if (!ok) failures++;
    }

    char summary[128];
    snprintf(summary, sizeof(summary), "\n%s - %d tests failed\n", failures ? "SOME TESTS FAILED" : "ALL TESTS PASSED", failures);
    printf("%s", summary);
    if (out) { fputs(summary, out); fclose(out); }
    return failures;
}

// ═══ entry point ═══
// tell Windows we render at native pixels: no bitmap upscaling / blur on high-DPI
static void enableDpiAwareness() {
    HMODULE u32 = GetModuleHandleA("user32.dll");
    if (u32) {
        typedef BOOL(WINAPI * SetCtxFn)(HANDLE);
        auto setCtx = (SetCtxFn)GetProcAddress(u32, "SetProcessDpiAwarenessContext");
        if (setCtx) {
            // PER_MONITOR_AWARE_V2 = (HANDLE)-4, fallback PER_MONITOR_AWARE = (HANDLE)-3
            if (setCtx((HANDLE)-4) || setCtx((HANDLE)-3)) return;
        }
        typedef BOOL(WINAPI * SpdaFn)(void);
        auto spda = (SpdaFn)GetProcAddress(u32, "SetProcessDPIAware");
        if (spda) spda();
    }
}

int WINAPI WinMain(HINSTANCE hInst, HINSTANCE, LPSTR, int) {
    enableDpiAwareness();
    bool testMode = false, shotMode = false;
    int shotDemo = 1;
    for (int i = 1; i < __argc; i++) {
        if (strcmp(__argv[i], "--test") == 0) testMode = true;
        else if (strcmp(__argv[i], "--screenshot") == 0) {
            shotMode = true;
            if (i + 1 < __argc) shotDemo = atoi(__argv[i + 1]);
        }
    }

    initDirs();
    loadStandaloneManifest();
    gBPLayers = &g.scene.layers;   // let the blueprint trace node list the scene layers

    if (testMode) {
        if (AttachConsole(ATTACH_PARENT_PROCESS)) {
            FILE* dummy;
            freopen_s(&dummy, "CONOUT$", "w", stdout);
        }
        return runTests();
    }

    loadEditorPreferences();

    // dock layout
    // level-editor panels: they live only around the viewport (the Level tab)
    g.dock.addWindow("viewport", "Viewport", DOCK_CENTER, 0);
    g.dock.addWindow("outliner", "Outliner", DOCK_LEFT, 0);
    g.dock.addWindow("dettagli", "Details", DOCK_RIGHT, 0);
    g.dock.addWindow("log", "Log", DOCK_BOTTOM, 0);
    g.dock.addWindow("contenuti", "Content", DOCK_BOTTOM, 1);
    g.dock.addWindow("navigation", "Navigation", DOCK_BOTTOM, 3);
    g.dock.addWindow("animation", "Animation", DOCK_BOTTOM, 4);
    g.dock.addWindow("animator", "Animator Controller", DOCK_BOTTOM, 5);
    // when the viewport floats over other panels, the dock asks us to repaint the 3D
    // on top of them (clearAll=false, so it overlays instead of wiping the UI)
    g.dock.renderViewportOverlay = [](const UIRect& cr) {
        if (!g.frameForRender || cr.w < 4 || cr.h < 4) return;
        g.renderer.render(*g.frameForRender, g.camera, (int)cr.x, (int)cr.y, (int)cr.w, (int)cr.h, false);
    };
    {
        std::string layout;
        if (readFile(g.baseDir + "\\layout.cfg", layout)) g.dock.loadLayout(layout);
    }

    if (!createGLWindow(hInst, !shotMode)) {
        MessageBoxA(nullptr, "Failed to create the OpenGL window/context", "Pulse Engine", MB_ICONERROR);
        return 1;
    }
    if (!g.renderer.init()) return 1;
    g.renderer.resize(g.width, g.height);
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);   // for the folder picker dialog
    loadAssetIcons();
    if (!g.standaloneMode) loadHub();

    g.activeDoc = 0;   // start on the Level tab; blueprints open as tabs on demand

    addLog(1, "Welcome to Pulse Engine! Press Play, then SPACE for the jumping cube.");
    addLog(0, "Drag the window tabs to detach and re-dock them like in Unreal.");
    addLog(0, "Ctrl+Z undo, Ctrl+X redo (50 steps); Ctrl+C/V copy and paste, Ctrl+D duplicate.");

    if (shotMode) {
        g.inHub = false;                 // screenshot: bypass the hub, use the sample project
        g.projectName = "progetto";
        scanBrowser();
        switch (shotDemo) {
        case 0: sceneDefault(g.scene); break;
        case 2: scenePendulums(g.scene); break;
        case 3: sceneDomino(g.scene); break;
        default: sceneTower(g.scene); break;
        }
        play();
        for (int i = 0; i < 84; i++) g.scene.world.step(FIXED_DT);
        if (g.scene.entities.size() > 1) g.selectedId = g.scene.entities[1].id;
        // TEMP verifica #18: variabili esposte Object/Transform con campo drop nei Details
        {
            BPGraph demoG;
            BPVarDef vo; vo.type = PIN_ENT; vo.scope = VS_PUBLIC; vo.expose = true; snprintf(vo.name, 32, "bersaglio"); demoG.vars.push_back(vo);
            BPVarDef vtf; vtf.type = PIN_TRANSFORM; vtf.scope = VS_PUBLIC; vtf.expose = true; snprintf(vtf.name, 32, "spawn"); demoG.vars.push_back(vtf);
            BPVarDef vf; vf.type = PIN_NUM; vf.scope = VS_PUBLIC; vf.expose = true; snprintf(vf.name, 32, "velocita"); vf.def = { 5, 0, 0 }; demoG.vars.push_back(vf);
            g.bpEditCache["Demo.bp"] = demoG;
            if (g.scene.entities.size() > 1) {
                Entity& te = g.scene.entities[1];
                snprintf(te.graphPath, sizeof(te.graphPath), "Demo.bp");
                if (g.scene.entities.size() > 2) te.varOverrides["bersaglio"] = { (float)g.scene.entities[2].id, 0, 0 };
                g.selectedId = te.id;
            }
        }
        // demo graph in a blueprint tab, contenuti floating
        BPEditor& demoBP = newBlueprintDoc();
        {
            BPGraph& bgra = demoBP.graph;
            bgra.clear();
            BPVarDef vd;
            snprintf(vd.name, sizeof(vd.name), "salti");
            bgra.vars.push_back(vd);
            BPCanvas& bg = bgra.main();
            int ev = bg.addNode(BP_EV_KEY, 30, 40);
            int once = bg.addNode(BP_FLOW_DOONCE, 250, 40);
            int imp = bg.addNode(BP_ACT_IMPULSE, 470, 30);
            bg.byId(imp)->lit[1] = { 0, 6, 0 };
            bg.connect(ev, 0, once, 0);
            bg.connect(once, 0, imp, 0);
            int hit = bg.addNode(BP_EV_HIT, 30, 180);
            int cmp = bg.addNode(BP_L_CMP, 260, 220);
            int se = bg.addNode(BP_L_IF, 470, 170);
            int col = bg.addNode(BP_ACT_COLOR, 690, 160);
            bg.byId(col)->lit[1] = { 1, 0.2f, 0.2f };
            bg.byId(cmp)->lit[1] = { 1.5f, 0, 0 };
            bg.connect(hit, 0, se, 0);
            bg.connect(hit, 1, cmp, 0);
            bg.connect(cmp, 0, se, 1);
            bg.connect(se, 0, col, 0);
        }
        g.dock.bottomH = 320;
        g.activeDoc = 0;   // show the Level (viewport) tab for the screenshot
        DockWindow* cw = g.dock.find("contenuti");
        if (cw) { cw->open = true; cw->area = DOCK_FLOAT; cw->rect = { 430, 60, 560, 300 }; }
        g.camera.target = { 0, 3, 0 };
        g.camera.distance = 20;
        g.camera.yaw = -1.1f;
        Frame frame;
        UIRect vp = viewportRect();
        bool haveVp = vp.w > 4 && vp.h > 4;
        g.camera.update(haveVp ? vp.w / vp.h : (float)g.width / g.height);
        buildFrame(frame);
        g.frameForRender = &frame;
        g.renderer.render(frame, g.camera, (int)vp.x, (int)vp.y, (int)vp.w, (int)vp.h);
        drawEditorUI();
        // second pass so panels settle (scroll extents computed on first frame)
        vp = viewportRect();
        buildFrame(frame);
        g.frameForRender = &frame;
        g.renderer.render(frame, g.camera, (int)vp.x, (int)vp.y, (int)vp.w, (int)vp.h);
        drawEditorUI();
        glFinish();
        saveBMP("screenshot.bmp", g.width, g.height);
        SwapBuffers(g.hdc);
        return 0;
    }

    // Editor starts on the hub. A packaged build instead loads the first scene
    // from its ordered manifest and immediately enters Play mode.
    if (g.standaloneMode) {
        g.inHub = false;
        std::string startup = g.projectDir + "\\" + g.standaloneScenes.front();
        std::string sceneData;
        if (!readFile(startup, sceneData) || !g.scene.deserialize(sceneData)) {
            std::string err = "Could not load the build's startup scene:\n" + startup;
            MessageBoxA(g.hwnd, err.c_str(), "Pulse Engine Build", MB_OK | MB_ICONERROR);
            return 2;
        }
        snprintf(g.projectPath, MAX_PATH, "%s", startup.c_str());
        std::string title = g.projectName + " - Pulse Engine";
        SetWindowTextA(g.hwnd, title.c_str());
        play();
    } else {
        g.inHub = true;
    }
    g.camera.target = { 0, 2, 0 };
    g.camera.distance = 15;

    LARGE_INTEGER freq, prev, now;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&prev);

    Frame frame;
    MSG msg;
    while (g.running) {
        std::string sceneHistoryBefore;
        if (!g.inHub && g.mode == Mode::Edit) sceneHistoryBefore = g.scene.serialize();
        g.uiIn.mousePressed = false;
        g.uiIn.mouseReleased = false;
        g.uiIn.rmbPressed = false;
        g.uiIn.rmbReleased = false;
        g.uiIn.mmbPressed = false;
        g.uiIn.mmbReleased = false;
        g.uiIn.wheel = 0;
        g.uiIn.typedCount = 0;
        g.browserDeletePending = false;   // consumed during the browser draw, else drop it
        g.uiIn.keyBackspace = false;
        g.uiIn.keyEnter = false;
        g.uiIn.keyEscape = false;
        g.uiIn.keyDelete = false;
        g.uiIn.keyLeft = false;
        g.uiIn.keyRight = false;
        g.uiIn.keySelectAll = false;
        g.uiIn.keyCopy = false;
        g.uiIn.keyPaste = false;
        g.uiIn.keyDuplicate = false;
        g.uiIn.keyPressedVK = 0;
        g.uiIn.keyCtrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
        g.uiIn.keyAlt = (GetKeyState(VK_MENU) & 0x8000) != 0;
        g.uiIn.keyShift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;

        while (PeekMessageA(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageA(&msg);
        }
        QueryPerformanceCounter(&now);
        float dt = (float)((double)(now.QuadPart - prev.QuadPart) / freq.QuadPart);
        prev = now;
        if (dt > 0.05f) dt = 0.05f;
        g.frameDt = dt > 0 ? dt : FIXED_DT;
        if (dt > 0) g.fps = g.fps * 0.95f + (1.0f / dt) * 0.05f;

        // ── hub screen: no scene / simulation, just the launcher ──
        if (g.inHub) {
            glViewport(0, 0, g.width, g.height);
            glDisable(GL_SCISSOR_TEST);
            glClearColor(0.07f, 0.08f, 0.10f, 1);
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
            g.ui.begin(&g.renderer, g.uiIn);
            drawHub();
            g.ui.end();
            SwapBuffers(g.hdc);
            continue;
        }

        updatePlayMouse();   // poll + recentre the captured cursor, feed look axes
        if (g.mode == Mode::Play && !g.paused) stepSim(dt);
        // expire trace-debug segments
        for (size_t i = 0; i < g.debugSegs.size();) {
            g.debugSegs[i].life -= dt;
            if (g.debugSegs[i].life <= 0) g.debugSegs.erase(g.debugSegs.begin() + i);
            else i++;
        }

        // Play view: game camera, or a free-fly camera when there is none / on eject
        updatePlayCamera(dt);
        updateEditFly(dt);   // Edit-mode WASD navigation while right-dragging the viewport
        updateAudioSources(dt);
        // render the 3D into the viewport tab's rect (aspect follows it, so it
        // recentres/refits when the tab is resized or moved)
        UIRect vp = viewportRect();
        bool haveVp = vp.w > 4 && vp.h > 4;
        g.camera.update(haveVp ? vp.w / vp.h : (float)g.width / (float)g.height);
        buildFrame(frame);
        g.frameForRender = &frame;
        // A floating/native viewport is drawn later at its own z-order (dock overlay
        // callback, or a native window). Here we only clear so the docked panels have
        // a clean background; rendering the 3D now would just be overpainted.
        DockWindow* vpw = g.dock.find("viewport");
        bool vpElsewhere = vpw && vpw->open && (vpw->area == DOCK_FLOAT || vpw->area == DOCK_NATIVE);
        if (vpElsewhere) g.renderer.render(frame, g.camera, 0, 0, 0, 0);
        else g.renderer.render(frame, g.camera, (int)vp.x, (int)vp.y, (int)vp.w, (int)vp.h);
        g.animationPanelDrawnThisFrame = false;
        drawEditorUI();
        SwapBuffers(g.hdc);

        renderNativeWindows();
        handleNativeWindows();
        if (g.animationPanelWasDrawn && !g.animationPanelDrawnThisFrame) {
            g.animationPlaying = false;
            g.animationRecording = false;
            restoreAnimationPreview();
        }
        g.animationPanelWasDrawn = g.animationPanelDrawnThisFrame;

        // blueprint "assign to selection" request (from any open blueprint tab)
        for (auto& ed : g.bpDocs) {
            if (!ed->assignRequested) continue;
            ed->assignRequested = false;
            Entity* sel = g.scene.byId(g.selectedId);
            if (!sel) addLog(2, "Select an object to assign the graph to first.");
            else if (ed->curPath.empty()) addLog(2, "Save the graph first (Save button in the Blueprint).");
            else {
                bool added = addBlueprintComponent(*sel, ed->curPath);
                addLog(added ? 1 : 2, added ? "Component %s added to %s."
                                            : "Component %s is single-instance and is already on %s.",
                       fs::path(ed->curPath).stem().string().c_str(), sel->name);
            }
        }
        finishSceneHistoryFrame(sceneHistoryBefore);
    }

    if (!g.standaloneMode) {
        writeFile(g.baseDir + "\\layout.cfg", g.dock.saveLayout());
        saveEditorPreferences();
    }
    // remember the open project's current level for next launch
    if (!g.standaloneMode && !g.inHub && !g.projectDir.empty()) {
        std::string rel;
        if (g.projectPath[0]) {
            std::string abs = g.projectPath;
            if (abs.rfind(g.projectDir, 0) == 0 && abs.size() > g.projectDir.size() + 1)
                rel = abs.substr(g.projectDir.size() + 1);
        }
        hubAdd(g.projectDir, rel);
    }
    return 0;
}
