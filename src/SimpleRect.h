#ifndef SIMPLE_RECT_H_
#define SIMPLE_RECT_H_

#ifdef __cplusplus
extern "C"
{
#endif

typedef struct RectI {
    int x, y;
    int dx, dy;
} RectI;

typedef struct RectD {
    double x,y;
    double dx,dy;
} RectD;

int    RectI_Intersect(RectI *r1, RectI *r2, RectI *rIntersectOut);
void   RectI_FromXY(RectI *rOut, int xs, int xe, int ys, int ye);
int    RectI_Inside(RectI *r, int x, int y);
void   RectD_FromXY(RectD *rOut, double xs, double xe,  double ys, double ye);
void   RectD_FromRectI(RectD *rOut, RectI *rIn);
void   u_RectI_Intersect(void);

#ifdef __cplusplus
}
#endif

/* allow using from both C and C++ code */
#ifdef __cplusplus
class PointD {
public:
    PointD() { x = 0; y = 0; }
    PointD(double _x, double _y) { x = _x; y = _y; }
    void set(double _x, double _y) { x = _x; y = _y; }
    double x;
    double y;
};

class SizeD {
public:
    SizeD(double _dx, double _dy) { dx = _dx; dy = _dy; }
    SizeD() { dx = 0; dy = 0; }
    void set(double _dx, double _dy) { dx = _dx; dy = _dy; }
    double dx;
    double dy;
};

class SizeI {
public:
    SizeI(int _dx, int _dy) { dx = _dx; dy = _dy; }
    void set(int _dx, int _dy) { dx = _dx; dy = _dy; }
    int dx;
    int dy;
};
#endif

#endif
