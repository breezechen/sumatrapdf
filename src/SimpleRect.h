#ifndef SIMPLE_RECT_H_
#define SIMPLE_RECT_H_

#ifdef __cplusplus
extern "C"
{
#endif

typedef struct SimpleRect {
    int x, y;
    int dx, dy;
} SimpleRect;

int    SimpleRect_Intersect(SimpleRect *r1, SimpleRect *r2, SimpleRect *rIntersectOut);
void   SimpleRect_FromXY(SimpleRect *rOut, int xs, int xe, int ys, int ye);
void   u_SimpleRect_Intersect(void);

#ifdef __cplusplus
}
#endif

#endif
