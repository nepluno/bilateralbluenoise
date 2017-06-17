#define BLOCK_LEVEL0 4
#define BLOCK_SIZE0 (1 << BLOCK_LEVEL0)

#define BLOCK_LEVEL1 7
#define BLOCK_SIZE1 (1 << BLOCK_LEVEL1)

#define G_POINT_LEVEL 10

#define FUZZY_BLUE_NOISE 6
#define SCROLL_VALUE

#define DDA_SIZE 128
#define mDensity 224.01450f

#define BOX_LEFT 0.001302083f
#define BOX_RIGHT 0.99869792f
#define BOX_TOP 0.001302083f
#define BOX_BOTTOM 0.99869792f

#define BOX_LEN_X (BOX_RIGHT - BOX_LEFT)
#define BOX_LEN_Y (BOX_BOTTOM - BOX_TOP)

#define BOX_LEN_X_DT (BOX_LEN_X * 0.9f)
#define BOX_LEN_Y_DT (BOX_LEN_Y * 0.9f)

#define BOX_LEFT_DT (BOX_LEFT + 0.05f * BOX_LEN_X)
#define BOX_TOP_DT (BOX_TOP + 0.05f * BOX_LEN_Y)

//#define BIN_DENSITY 4
//#define RANDOM_DISTURBING
//#define SHOW_BORDER
#define UNKNOWN_DENSITY