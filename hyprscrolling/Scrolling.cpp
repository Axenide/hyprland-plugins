#include "Scrolling.hpp"

#include <hyprland/src/Compositor.hpp>
#include <hyprland/src/config/ConfigManager.hpp>
#include <hyprland/src/config/ConfigValue.hpp>
#include <hyprland/src/render/Renderer.hpp>

#include <hyprutils/string/ConstVarList.hpp>
using namespace Hyprutils::String;

constexpr float MIN_COLUMN_WIDTH = 0.05F;
constexpr float MAX_COLUMN_WIDTH = 1.F;
constexpr float MIN_ROW_HEIGHT   = 0.1F;
constexpr float MAX_ROW_HEIGHT   = 1.F;

//
void SColumnData::add(PHLWINDOW w) {
    for (auto& wd : windowDatas) {
        wd->windowSize *= (float)windowDatas.size() / (float)(windowDatas.size() + 1);
    }

    windowDatas.emplace_back(makeShared<SScrollingWindowData>(w, self.lock(), 1.F / (float)(windowDatas.size() + 1)));
}

void SColumnData::add(SP<SScrollingWindowData> w) {
    for (auto& wd : windowDatas) {
        wd->windowSize *= (float)windowDatas.size() / (float)(windowDatas.size() + 1);
    }

    windowDatas.emplace_back(w);
    w->column     = self;
    w->windowSize = 1.F / (float)(windowDatas.size());
}

void SColumnData::remove(PHLWINDOW w) {
    const auto SIZE_BEFORE = windowDatas.size();
    std::erase_if(windowDatas, [&w](const auto& e) { return e->window == w; });

    if (SIZE_BEFORE == windowDatas.size() && SIZE_BEFORE > 0)
        return;

    float newMaxSize = 0.F;
    for (auto& wd : windowDatas) {
        newMaxSize += wd->windowSize;
    }

    for (auto& wd : windowDatas) {
        wd->windowSize *= 1.F / newMaxSize;
    }

    if (windowDatas.empty() && workspace)
        workspace->remove(self.lock());
}

void SColumnData::up(SP<SScrollingWindowData> w) {
    for (size_t i = 1; i < windowDatas.size(); ++i) {
        if (windowDatas[i] != w)
            continue;

        std::swap(windowDatas[i], windowDatas[i - 1]);
    }
}

void SColumnData::down(SP<SScrollingWindowData> w) {
    for (size_t i = 0; i < windowDatas.size() - 1; ++i) {
        if (windowDatas[i] != w)
            continue;

        std::swap(windowDatas[i], windowDatas[i + 1]);
    }
}

SP<SScrollingWindowData> SColumnData::next(SP<SScrollingWindowData> w) {
    for (size_t i = 0; i < windowDatas.size() - 1; ++i) {
        if (windowDatas[i] != w)
            continue;

        return windowDatas[i + 1];
    }

    return nullptr;
}

SP<SScrollingWindowData> SColumnData::prev(SP<SScrollingWindowData> w) {
    for (size_t i = 1; i < windowDatas.size(); ++i) {
        if (windowDatas[i] != w)
            continue;

        return windowDatas[i - 1];
    }

    return nullptr;
}

SP<SColumnData> SWorkspaceData::add() {
    static const auto PCOLWIDTH = CConfigValue<Hyprlang::FLOAT>("plugin:hyprscrolling:column_width");
    auto              col       = columns.emplace_back(makeShared<SColumnData>(self.lock()));
    col->self                   = col;
    col->columnWidth            = *PCOLWIDTH;
    return col;
}

void SWorkspaceData::remove(SP<SColumnData> c) {
    std::erase(columns, c);
}

SP<SColumnData> SWorkspaceData::next(SP<SColumnData> c) {
    for (size_t i = 0; i < columns.size(); ++i) {
        if (columns[i] != c)
            continue;

        if (i == columns.size() - 1)
            return nullptr;

        return columns[i + 1];
    }

    return nullptr;
}

SP<SColumnData> SWorkspaceData::prev(SP<SColumnData> c) {
    for (size_t i = 0; i < columns.size(); ++i) {
        if (columns[i] != c)
            continue;

        if (i == 0)
            return nullptr;

        return columns[i - 1];
    }

    return nullptr;
}

void SWorkspaceData::centerCol(SP<SColumnData> c) {
    if (!c)
        return;

    static const auto PFSONONE = CConfigValue<Hyprlang::INT>("plugin:hyprscrolling:fullscreen_on_one_column");

    PHLMONITOR        PMONITOR    = workspace->m_monitor.lock();
    double            currentLeft = 0;

    for (const auto& COL : columns) {
        const double ITEM_WIDTH = *PFSONONE && columns.size() == 1 ? PMONITOR->m_size.x : PMONITOR->m_size.x * COL->columnWidth;

        if (COL != c)
            currentLeft += ITEM_WIDTH;
        else {
            leftOffset = -currentLeft + ITEM_WIDTH / 2.0;
            return;
        }
    }
}

SP<SColumnData> SWorkspaceData::atCenter() {
    static const auto PFSONONE = CConfigValue<Hyprlang::INT>("plugin:hyprscrolling:fullscreen_on_one_column");

    PHLMONITOR        PMONITOR    = workspace->m_monitor.lock();
    double            currentLeft = leftOffset;

    for (const auto& COL : columns) {
        const double ITEM_WIDTH = *PFSONONE && columns.size() == 1 ? PMONITOR->m_size.x : PMONITOR->m_size.x * COL->columnWidth;

        currentLeft += ITEM_WIDTH;

        if (currentLeft >= PMONITOR->m_size.x / 2.0 - 2)
            return COL;
    }

    return nullptr;
}

void SWorkspaceData::recalculate() {
    static const auto PFSONONE = CConfigValue<Hyprlang::INT>("plugin:hyprscrolling:fullscreen_on_one_column");

    if (!workspace || !workspace) {
        Debug::log(ERR, "[scroller] broken internal state on workspace data");
        return;
    }

    leftOffset = std::clamp((double)leftOffset, -maxWidth(), 0.0);

    const auto MAX_WIDTH = maxWidth();

    PHLMONITOR PMONITOR = workspace->m_monitor.lock();

    const CBox USABLE = layout->usableAreaFor(PMONITOR);

    double     currentLeft = MAX_WIDTH < USABLE.w ? std::round((USABLE.w - MAX_WIDTH) / 2.0) : leftOffset; // layout pixels

    for (const auto& COL : columns) {
        double       currentTop = 0.0;
        const double ITEM_WIDTH = *PFSONONE && columns.size() == 1 ? USABLE.w : USABLE.w * COL->columnWidth;

        for (const auto& WINDOW : COL->windowDatas) {
            WINDOW->layoutBox = CBox{currentLeft, currentTop, ITEM_WIDTH, WINDOW->windowSize * USABLE.h}.translate(PMONITOR->m_position + PMONITOR->m_reservedTopLeft);

            currentTop += WINDOW->windowSize * USABLE.h;

            layout->applyNodeDataToWindow(WINDOW, false);
        }

        currentLeft += ITEM_WIDTH;
    }
}

double SWorkspaceData::maxWidth() {
    static const auto PFSONONE = CConfigValue<Hyprlang::INT>("plugin:hyprscrolling:fullscreen_on_one_column");

    PHLMONITOR        PMONITOR    = workspace->m_monitor.lock();
    double            currentLeft = 0;

    for (const auto& COL : columns) {
        const double ITEM_WIDTH = *PFSONONE && columns.size() == 1 ? PMONITOR->m_size.x : PMONITOR->m_size.x * COL->columnWidth;

        currentLeft += ITEM_WIDTH;
    }

    return currentLeft;
}

void CScrollingLayout::applyNodeDataToWindow(SP<SScrollingWindowData> data, bool force) {
    if (!data || !data->column || !data->column->workspace) {
        Debug::log(ERR, "[scroller] broken internal state on node");
        return;
    }

    PHLMONITOR PMONITOR = data->column->workspace->workspace ? data->column->workspace->workspace->m_monitor.lock() : nullptr;

    if (!PMONITOR) {
        Debug::log(ERR, "[scroller] broken internal state on workspace");
        return;
    }

    // for gaps outer
    const bool DISPLAYLEFT   = STICKS(data->layoutBox.x, PMONITOR->m_position.x + PMONITOR->m_reservedTopLeft.x);
    const bool DISPLAYRIGHT  = STICKS(data->layoutBox.x + data->layoutBox.w, PMONITOR->m_position.x + PMONITOR->m_size.x - PMONITOR->m_reservedBottomRight.x);
    const bool DISPLAYTOP    = STICKS(data->layoutBox.y, PMONITOR->m_position.y + PMONITOR->m_reservedTopLeft.y);
    const bool DISPLAYBOTTOM = STICKS(data->layoutBox.y + data->layoutBox.h, PMONITOR->m_position.y + PMONITOR->m_size.y - PMONITOR->m_reservedBottomRight.y);

    const auto PWINDOW = data->window.lock();
    // get specific gaps and rules for this workspace,
    // if user specified them in config
    const auto WORKSPACERULE = g_pConfigManager->getWorkspaceRuleFor(g_pCompositor->getWorkspaceByID(data->column->workspace->workspace->m_id));

    if (!validMapped(PWINDOW)) {
        Debug::log(ERR, "Node {} holding invalid {}!!", (uintptr_t)data.get(), PWINDOW);
        onWindowRemovedTiling(PWINDOW);
        return;
    }

    if (PWINDOW->isFullscreen())
        return;

    PWINDOW->unsetWindowData(PRIORITY_LAYOUT);
    PWINDOW->updateWindowData();

    static auto PGAPSINDATA  = CConfigValue<Hyprlang::CUSTOMTYPE>("general:gaps_in");
    static auto PGAPSOUTDATA = CConfigValue<Hyprlang::CUSTOMTYPE>("general:gaps_out");
    auto* const PGAPSIN      = (CCssGapData*)(PGAPSINDATA.ptr())->getData();
    auto* const PGAPSOUT     = (CCssGapData*)(PGAPSOUTDATA.ptr())->getData();

    auto        gapsIn  = WORKSPACERULE.gapsIn.value_or(*PGAPSIN);
    auto        gapsOut = WORKSPACERULE.gapsOut.value_or(*PGAPSOUT);
    CBox        nodeBox = data->layoutBox;
    nodeBox.round();

    PWINDOW->m_size     = nodeBox.size();
    PWINDOW->m_position = nodeBox.pos();

    PWINDOW->updateWindowDecos();

    auto       calcPos  = PWINDOW->m_position;
    auto       calcSize = PWINDOW->m_size;

    const auto OFFSETTOPLEFT = Vector2D((double)(DISPLAYLEFT ? gapsOut.m_left : gapsIn.m_left), (double)(DISPLAYTOP ? gapsOut.m_top : gapsIn.m_top));

    const auto OFFSETBOTTOMRIGHT = Vector2D((double)(DISPLAYRIGHT ? gapsOut.m_right : gapsIn.m_right), (double)(DISPLAYBOTTOM ? gapsOut.m_bottom : gapsIn.m_bottom));

    calcPos  = calcPos + OFFSETTOPLEFT;
    calcSize = calcSize - OFFSETTOPLEFT - OFFSETBOTTOMRIGHT;

    if (PWINDOW->m_isPseudotiled) {
        // Calculate pseudo
        float scale = 1;

        // adjust if doesnt fit
        if (PWINDOW->m_pseudoSize.x > calcSize.x || PWINDOW->m_pseudoSize.y > calcSize.y) {
            if (PWINDOW->m_pseudoSize.x > calcSize.x) {
                scale = calcSize.x / PWINDOW->m_pseudoSize.x;
            }

            if (PWINDOW->m_pseudoSize.y * scale > calcSize.y) {
                scale = calcSize.y / PWINDOW->m_pseudoSize.y;
            }

            auto DELTA = calcSize - PWINDOW->m_pseudoSize * scale;
            calcSize   = PWINDOW->m_pseudoSize * scale;
            calcPos    = calcPos + DELTA / 2.f; // center
        } else {
            auto DELTA = calcSize - PWINDOW->m_pseudoSize;
            calcPos    = calcPos + DELTA / 2.f; // center
            calcSize   = PWINDOW->m_pseudoSize;
        }
    }

    const auto RESERVED = PWINDOW->getFullWindowReservedArea();
    calcPos             = calcPos + RESERVED.topLeft;
    calcSize            = calcSize - (RESERVED.topLeft + RESERVED.bottomRight);

    if (PWINDOW->onSpecialWorkspace() && !PWINDOW->isFullscreen()) {
        // if special, we adjust the coords a bit
        static auto PSCALEFACTOR = CConfigValue<Hyprlang::FLOAT>("dwindle:special_scale_factor");

        CBox        wb = {calcPos + (calcSize - calcSize * *PSCALEFACTOR) / 2.f, calcSize * *PSCALEFACTOR};
        wb.round(); // avoid rounding mess

        *PWINDOW->m_realPosition = wb.pos();
        *PWINDOW->m_realSize     = wb.size();
    } else {
        CBox wb = {calcPos, calcSize};
        wb.round(); // avoid rounding mess

        *PWINDOW->m_realSize     = wb.size();
        *PWINDOW->m_realPosition = wb.pos();
    }

    if (force) {
        g_pHyprRenderer->damageWindow(PWINDOW);

        PWINDOW->m_realPosition->warp();
        PWINDOW->m_realSize->warp();

        g_pHyprRenderer->damageWindow(PWINDOW);
    }

    PWINDOW->updateWindowDecos();
}

void CScrollingLayout::onEnable() {
    static const auto PCONFWIDTHS = CConfigValue<Hyprlang::STRING>("plugin:hyprscrolling:explicit_column_widths");

    m_configCallback = g_pHookSystem->hookDynamic("configReloaded", [this](void* hk, SCallbackInfo& info, std::any param) {
        // bitch ass
        m_config.configuredWidths.clear();

        CConstVarList widths(*PCONFWIDTHS, 0, ',');
        for (auto& w : widths) {
            try {
                m_config.configuredWidths.emplace_back(std::stof(std::string{w}));
            } catch (...) { Debug::log(ERR, "scrolling: Failed to parse width {} as float", w); }
        }
    });

    for (auto const& w : g_pCompositor->m_windows) {
        if (w->m_isFloating || !w->m_isMapped || w->isHidden())
            continue;

        onWindowCreatedTiling(w);
    }
}

void CScrollingLayout::onDisable() {
    m_workspaceDatas.clear();
    m_configCallback.reset();
}

void CScrollingLayout::onWindowCreatedTiling(PHLWINDOW window, eDirection direction) {
    auto workspaceData = dataFor(window->m_workspace);

    if (!workspaceData) {
        Debug::log(LOG, "[scrolling] No workspace data yet, creating");
        workspaceData       = m_workspaceDatas.emplace_back(makeShared<SWorkspaceData>(window->m_workspace, this));
        workspaceData->self = workspaceData;
    }

    const auto                     PLASTFOCUS      = g_pCompositor->m_lastWindow.lock();
    const SP<SScrollingWindowData> LAST_FOCUS_DATA = PLASTFOCUS ? dataFor(PLASTFOCUS) : nullptr;
    const auto                     PMONITOR        = window->m_monitor;

    bool                           addNewColumn = !PLASTFOCUS || !LAST_FOCUS_DATA || PLASTFOCUS->m_workspace != window->m_workspace || workspaceData->columns.size() <= 1;

    if (!addNewColumn && PMONITOR) {
        if (workspaceData->atCenter() == nullptr)
            addNewColumn = true;
    }

    Debug::log(LOG, "[scrolling] new window {:x}, addNewColumn: {}, columns before: {}", (uintptr_t)window.get(), addNewColumn, workspaceData->columns.size());

    if (addNewColumn) {
        auto col = workspaceData->add();
        col->add(window);
    } else {
        // LAST_FOCUS_DATA has to be valid
        auto col = LAST_FOCUS_DATA->column;
        col->add(window);
    }

    workspaceData->recalculate();
}

void CScrollingLayout::onWindowRemovedTiling(PHLWINDOW window) {
    const auto DATA = dataFor(window);

    if (!DATA)
        return;

    DATA->column->remove(window);
}

bool CScrollingLayout::isWindowTiled(PHLWINDOW window) {
    const auto DATA = dataFor(window);

    return DATA;
}

void CScrollingLayout::recalculateMonitor(const MONITORID& id) {
    const auto PMONITOR = g_pCompositor->getMonitorFromID(id);
    if (!PMONITOR || !PMONITOR->m_activeWorkspace)
        return;

    const auto DATA = dataFor(PMONITOR->m_activeWorkspace);

    if (!DATA)
        return;

    DATA->recalculate();
}

void CScrollingLayout::recalculateWindow(PHLWINDOW window) {
    if (!window->m_workspace)
        return;

    const auto DATA = dataFor(window->m_workspace);

    if (!DATA)
        return;

    DATA->recalculate();
}

void CScrollingLayout::onBeginDragWindow() {
    IHyprLayout::onBeginDragWindow();
}

void CScrollingLayout::resizeActiveWindow(const Vector2D& delta, eRectCorner corner, PHLWINDOW pWindow) {
    const auto PWINDOW = pWindow ? pWindow : g_pCompositor->m_lastWindow.lock();

    if (!validMapped(PWINDOW))
        return;

    const auto DATA = dataFor(PWINDOW);

    if (!DATA) {
        *PWINDOW->m_realSize =
            (PWINDOW->m_realSize->goal() + delta)
                .clamp(PWINDOW->m_windowData.minSize.valueOr(Vector2D{MIN_WINDOW_SIZE, MIN_WINDOW_SIZE}), PWINDOW->m_windowData.maxSize.valueOr(Vector2D{INFINITY, INFINITY}));
        PWINDOW->updateWindowDecos();
        return;
    }

    if (corner == CORNER_NONE)
        return;

    if (!DATA->column || !DATA->column->workspace || !DATA->column->workspace->workspace || !DATA->column->workspace->workspace->m_monitor)
        return;

    const auto USABLE        = usableAreaFor(DATA->column->workspace->workspace->m_monitor.lock());
    const auto DELTA_AS_PERC = delta / USABLE.size();

    const auto CURR_COLUMN = DATA->column.lock();
    const auto NEXT_COLUMN = DATA->column->workspace->next(CURR_COLUMN);
    const auto PREV_COLUMN = DATA->column->workspace->prev(CURR_COLUMN);

    switch (corner) {
        case CORNER_BOTTOMLEFT:
        case CORNER_TOPLEFT: {
            if (!PREV_COLUMN)
                break;

            PREV_COLUMN->columnWidth = std::clamp(PREV_COLUMN->columnWidth + (float)DELTA_AS_PERC.x, MIN_COLUMN_WIDTH, MAX_COLUMN_WIDTH);
            CURR_COLUMN->columnWidth = std::clamp(CURR_COLUMN->columnWidth - (float)DELTA_AS_PERC.x, MIN_COLUMN_WIDTH, MAX_COLUMN_WIDTH);
            break;
        }
        case CORNER_BOTTOMRIGHT:
        case CORNER_TOPRIGHT: {
            if (!NEXT_COLUMN)
                break;

            NEXT_COLUMN->columnWidth = std::clamp(NEXT_COLUMN->columnWidth - (float)DELTA_AS_PERC.x, MIN_COLUMN_WIDTH, MAX_COLUMN_WIDTH);
            CURR_COLUMN->columnWidth = std::clamp(CURR_COLUMN->columnWidth + (float)DELTA_AS_PERC.x, MIN_COLUMN_WIDTH, MAX_COLUMN_WIDTH);
            break;
        }

        default: break;
    }

    if (DATA->column->windowDatas.size() > 1) {
        const auto CURR_WD = DATA;
        const auto NEXT_WD = DATA->column->next(DATA);
        const auto PREV_WD = DATA->column->prev(DATA);

        switch (corner) {
            case CORNER_BOTTOMLEFT:
            case CORNER_BOTTOMRIGHT: {
                if (!NEXT_WD)
                    break;

                if (NEXT_WD->windowSize <= MIN_ROW_HEIGHT && delta.y >= 0)
                    break;

                float adjust = std::clamp((float)(delta.y / USABLE.h), (-CURR_WD->windowSize + MIN_ROW_HEIGHT), (NEXT_WD->windowSize - MIN_ROW_HEIGHT));

                NEXT_WD->windowSize = std::clamp(NEXT_WD->windowSize - adjust, MIN_ROW_HEIGHT, MAX_ROW_HEIGHT);
                CURR_WD->windowSize = std::clamp(CURR_WD->windowSize + adjust, MIN_ROW_HEIGHT, MAX_ROW_HEIGHT);
                break;
            }
            case CORNER_TOPLEFT:
            case CORNER_TOPRIGHT: {
                if (!PREV_WD)
                    break;

                if (PREV_WD->windowSize <= MIN_ROW_HEIGHT && delta.y <= 0 || CURR_WD->windowSize <= MIN_ROW_HEIGHT && delta.y >= 0)
                    break;

                float adjust = std::clamp((float)(delta.y / USABLE.h), -(PREV_WD->windowSize - MIN_ROW_HEIGHT), (CURR_WD->windowSize - MIN_ROW_HEIGHT));

                PREV_WD->windowSize = std::clamp(PREV_WD->windowSize + adjust, MIN_ROW_HEIGHT, MAX_ROW_HEIGHT);
                CURR_WD->windowSize = std::clamp(CURR_WD->windowSize - adjust, MIN_ROW_HEIGHT, MAX_ROW_HEIGHT);
                break;
            }

            default: break;
        }
    }

    DATA->column->workspace->recalculate();
}

void CScrollingLayout::fullscreenRequestForWindow(PHLWINDOW pWindow, const eFullscreenMode CURRENT_EFFECTIVE_MODE, const eFullscreenMode EFFECTIVE_MODE) {
    ;
}

std::any CScrollingLayout::layoutMessage(SLayoutMessageHeader header, std::string message) {
    const auto ARGS = CVarList(message, 0, ' ');
    if (ARGS[0] == "move") {
        const auto DATA = currentWorkspaceData();
        if (!DATA)
            return {};

        if (ARGS[1] == "+col" || ARGS[1] == "col") {
            const auto WDATA = dataFor(g_pCompositor->m_lastWindow.lock());
            if (!WDATA)
                return {};

            const auto COL = DATA->next(WDATA->column.lock());
            if (!COL) {
                // move to max
                DATA->leftOffset = -DATA->maxWidth();
                DATA->recalculate();
                g_pCompositor->focusWindow(nullptr);
                return {};
            }

            DATA->centerCol(COL);
            DATA->recalculate();

            g_pCompositor->focusWindow(COL->windowDatas.front()->window.lock());

            return {};
        } else if (ARGS[1] == "-col") {
            const auto WDATA = dataFor(g_pCompositor->m_lastWindow.lock());
            if (!WDATA) {
                if (DATA->leftOffset <= DATA->maxWidth() && DATA->columns.size() > 0) {
                    DATA->centerCol(DATA->columns.back());
                    DATA->recalculate();
                    g_pCompositor->focusWindow((DATA->columns.back()->windowDatas.back())->window.lock());
                }

                return {};
            }

            const auto COL = DATA->prev(WDATA->column.lock());
            if (!COL)
                return {};
            DATA->centerCol(COL);
            DATA->recalculate();

            g_pCompositor->focusWindow(COL->windowDatas.back()->window.lock());

            return {};
        }

        const auto PLUSMINUS = getPlusMinusKeywordResult(ARGS[1], 0);

        if (!PLUSMINUS.has_value())
            return {};

        DATA->leftOffset -= *PLUSMINUS;
        DATA->recalculate();

        const auto ATCENTER = DATA->atCenter();

        g_pCompositor->focusWindow(ATCENTER ? (*ATCENTER->windowDatas.begin())->window.lock() : nullptr);
    } else if (ARGS[0] == "colresize") {
        const auto WDATA = dataFor(g_pCompositor->m_lastWindow.lock());

        if (!WDATA)
            return {};

        if (ARGS[1][0] == '+' || ARGS[1][0] == '-') {
            if (ARGS[1] == "+conf") {
                for (size_t i = 0; i < m_config.configuredWidths.size(); ++i) {
                    if (m_config.configuredWidths[i] < WDATA->column->columnWidth)
                        continue;

                    if (i == m_config.configuredWidths.size() - 1)
                        WDATA->column->columnWidth = m_config.configuredWidths[0];
                    else
                        WDATA->column->columnWidth = m_config.configuredWidths[i + 1];

                    break;
                }

                return {};
            } else if (ARGS[1] == "-conf") {
                for (size_t i = m_config.configuredWidths.size() - 1; i >= 0; --i) {
                    if (m_config.configuredWidths[i] > WDATA->column->columnWidth)
                        continue;

                    if (i == 0)
                        WDATA->column->columnWidth = m_config.configuredWidths[m_config.configuredWidths.size() - 1];
                    else
                        WDATA->column->columnWidth = m_config.configuredWidths[i - 1];

                    break;
                }

                return {};
            }

            const auto PLUSMINUS = getPlusMinusKeywordResult(ARGS[1], 0);

            if (!PLUSMINUS.has_value())
                return {};

            WDATA->column->columnWidth += *PLUSMINUS;
        } else {
            float abs = 0;
            try {
                abs = std::stof(ARGS[1]);
            } catch (...) { return {}; }

            WDATA->column->columnWidth = abs;
        }

        WDATA->column->columnWidth = std::clamp(WDATA->column->columnWidth, MIN_COLUMN_WIDTH, MAX_COLUMN_WIDTH);

        WDATA->column->workspace->recalculate();
    } else if (ARGS[0] == "movewindowto") {
        moveWindowTo(g_pCompositor->m_lastWindow.lock(), ARGS[1], false);
    }

    return {};
}

SWindowRenderLayoutHints CScrollingLayout::requestRenderHints(PHLWINDOW a) {
    return {};
}

void CScrollingLayout::switchWindows(PHLWINDOW a, PHLWINDOW b) {
    ;
}

void CScrollingLayout::moveWindowTo(PHLWINDOW w, const std::string& dir, bool silent) {
    const auto DATA = dataFor(w);

    if (!DATA)
        return;

    const auto WS = DATA->column->workspace.lock();

    if (dir == "l") {
        const auto COL = WS->prev(DATA->column.lock());
        if (!COL)
            return;

        DATA->column->remove(w);
        COL->add(DATA);
    } else if (dir == "r") {
        const auto COL = WS->next(DATA->column.lock());

        DATA->column->remove(w);

        if (!COL) {
            // make a new one
            const auto NEWCOL = WS->add();
            NEWCOL->add(DATA);
        } else
            COL->add(DATA);

    } else if (dir == "t" || dir == "u")
        DATA->column->up(DATA);
    else if (dir == "b" || dir == "d")
        DATA->column->down(DATA);

    WS->recalculate();
}

void CScrollingLayout::alterSplitRatio(PHLWINDOW, float, bool) {
    ;
}

std::string CScrollingLayout::getLayoutName() {
    return "scrolling";
}

void CScrollingLayout::replaceWindowDataWith(PHLWINDOW from, PHLWINDOW to) {
    ;
}

Vector2D CScrollingLayout::predictSizeForNewWindowTiled() {
    return Vector2D{};
}

SP<SWorkspaceData> CScrollingLayout::dataFor(PHLWORKSPACE ws) {
    for (const auto& e : m_workspaceDatas) {
        if (e->workspace != ws)
            continue;

        return e;
    }

    return nullptr;
}

SP<SScrollingWindowData> CScrollingLayout::dataFor(PHLWINDOW w) {
    if (!w)
        return nullptr;

    for (const auto& e : m_workspaceDatas) {
        if (e->workspace != w->m_workspace)
            continue;

        for (const auto& c : e->columns) {
            for (const auto& d : c->windowDatas) {
                if (d->window != w)
                    continue;

                return d;
            }
        }
    }

    return nullptr;
}

SP<SWorkspaceData> CScrollingLayout::currentWorkspaceData() {
    if (!g_pCompositor->m_lastMonitor || !g_pCompositor->m_lastMonitor->m_activeWorkspace)
        return nullptr;

    // FIXME: special

    return dataFor(g_pCompositor->m_lastMonitor->m_activeWorkspace);
}

CBox CScrollingLayout::usableAreaFor(PHLMONITOR m) {
    return CBox{m->m_reservedTopLeft, m->m_size - m->m_reservedTopLeft - m->m_reservedBottomRight};
}
