/* Private builtin effect execution interfaces. */
#ifndef __WINE_D2D_EFFECT_PRIVATE_H
#define __WINE_D2D_EFFECT_PRIVATE_H

struct d2d_effect_image
{
    struct d2d_bitmap *bitmap;
    D2D1_RECT_F logical_bounds;
    D2D1_RECT_F texture_bounds;
    D2D1_ALPHA_MODE alpha_mode;
};

#endif
