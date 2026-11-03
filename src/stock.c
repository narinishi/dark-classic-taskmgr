#include "common.h"

/* ==================================================================
   STOCK OBJECT LAYER (issues 3, 4, partly 2-T2).
   Evidence: taskmgr draws all bevels via MoveToEx/LineTo/Rectangle +
   pens from GetStockObject (tracergdi: CreatePen colors are ONLY
   saturated plot colors; no DrawEdge/FrameRect imports exist).
   WHITE_BRUSH(0)/LTGRAY_BRUSH(1) -> dark brush,
   WHITE_PEN(6) -> DC_EDGE pen (dark bevel line),
   BLACK_PEN(7) -> DC_GRAY pen (visible on dark graph bg).
   ================================================================== */
static PFN_GetStockObject p_real_GetStockObject;
static PFN_CreatePen p_real_CreatePen;
static HBRUSH g_stock_dark_brush;
static void* g_stock_edge_pen;
static void* g_stock_gray_pen;

/* Stock object mapping for DARK theme (fix: WHITE_PEN must stay
   VISIBLE as the bevel highlight on dark bg — DC_EDGE 2,2,2 made
   graph/button outlines invisible; BLACK_PEN is the shadow edge and
   can be subtle). */
void* hook_GetStockObject(int idx) {
    if (!on_ui_thread())
        return p_real_GetStockObject ? p_real_GetStockObject(idx) : 0;
    if ((idx == 0 || idx == 1) && g_stock_dark_brush) return g_stock_dark_brush;
    if (idx == 6 && g_stock_gray_pen) return g_stock_gray_pen;   /* WHITE_PEN -> visible gray bevel */
    if (idx == 7 && g_stock_edge_pen) return g_stock_edge_pen;   /* BLACK_PEN -> subtle shadow    */
    return p_real_GetStockObject ? p_real_GetStockObject(idx) : 0;
}

void stock_init(HMODULE g) {
    p_real_GetStockObject = (PFN_GetStockObject)resolve(g, "GetStockObject");
    p_real_CreatePen = (PFN_CreatePen)resolve(g, "CreatePen");
    if (p_CreateSolidBrush) g_stock_dark_brush = p_CreateSolidBrush(DC_BG);
    if (p_real_CreatePen) {
        g_stock_edge_pen = p_real_CreatePen(0, 1, DC_EDGE);   /* BLACK_PEN -> dark shadow edge */
        g_stock_gray_pen = p_real_CreatePen(0, 1, DC_GRAY);   /* WHITE_PEN -> visible gray bevel */
    }
}

void stock_attach(void) {
    if (p_real_GetStockObject)
        hook_attach((void**)&p_real_GetStockObject, (void*)&hook_GetStockObject);
}
