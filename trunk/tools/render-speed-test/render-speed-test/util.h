#ifndef util_h
#define util_h

// stuff that, once implemented, doesn't change often
namespace str {

size_t Utf8ToWcharBuf(const char *s, size_t sLen, WCHAR *bufOut, size_t cchBufOutSize);
char *DupN(char *s, size_t sLen);

}

// iterates over words of the string and for each word calls a function f(char *s, size_t sLen)
// it collapses multile white-space characters as one.
// it emits newline as '\n' and normalizes '\r' and '\r\n' into '\n' (but doesn't collapse
// multiple new-lines into one)
template <typename Func>
void IterWords(char *s, size_t sLen, Func f) {
    char *end = s + sLen;
    // TODO: could possibly be faster by normalizing nl while we go,
    // but it would complicate the code
    end = normalize_nl(s, end);
    char c;
    char *currStart;
    size_t nWords = 0;
    size_t nLines = 0;
    for (;;) {
        skip_ws(s, end);
        currStart = s;
        for (;;) {
            c = get_next(s, end);
            if (is_word_end(c))
                break;
        }
        auto len = s - currStart - 1;
        if (len > 0)
            f(currStart, len);
        nWords++;
        if (c == '\n') {
            f("\n", 1);
            nLines++;
        }
        if (0 == c)
            break;
    }
}

// http://kennykerr.ca/2014/03/29/classy-windows-2/s
template <typename T>
struct Window
{
    HWND m_window = nullptr;

    static T * GetThisFromHandle(HWND window)
    {
        return reinterpret_cast<T *>(GetWindowLongPtr(window,
            GWLP_USERDATA));
    }

    static LRESULT __stdcall WndProc(HWND window, UINT message, WPARAM wparam, LPARAM lparam)
    {
        //ASSERT(window);

        if (WM_NCCREATE == message)
        {
            CREATESTRUCT * cs = reinterpret_cast<CREATESTRUCT *>(lparam);
            T * that = static_cast<T *>(cs->lpCreateParams);
            //ASSERT(that);
            //ASSERT(!that->m_window);
            that->m_window = window;
            SetWindowLongPtr(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(that));
        }
        else if (T * that = GetThisFromHandle(window))
        {
            return that->MessageHandler(message, wparam, lparam);
        }

        return DefWindowProc(window, message, wparam, lparam);
    }

    LRESULT MessageHandler(UINT message, WPARAM wparam, LPARAM lparam)
    {
        if (WM_DESTROY == message)
        {
            PostQuitMessage(0);
            return 0;
        }

        return DefWindowProc(m_window, message, wparam, lparam);
    }
};

static inline bool is_ws(char c) {
    // TODO: probably more white-space characters
    return c == ' ' ||
        c == '\t';
}

static inline char get_next(char*& s, char *end) {
    if (s >= end)
        return 0;
    return *s++;
}

static inline char peek_next(char*& s, char *end) {
    if (s >= end)
        return 0;
    return *s;
}

static inline void skip_ws(char*& s, char *end) {
    while (s < end && is_ws(*s))
        s++;
}

static inline bool is_word_end(char c) {
    return is_ws(c) || c == '\n' || c == 0;
}

template <typename T>
T *AllocStruct(size_t n=0) {
    return (T*) calloc(n, sizeof(T));
}

void InitAllCommonControls();

class ScopedGdiPlus {
protected:
    Gdiplus::GdiplusStartupInput si;
    Gdiplus::GdiplusStartupOutput so;
    ULONG_PTR token, hookToken;
    bool noBgThread;

public:
    // suppress the GDI+ background thread when initiating in WinMain,
    // as that thread causes DDE messages to be sent too early and
    // thus causes unexpected timeouts
    explicit ScopedGdiPlus(bool inWinMain = false) : noBgThread(inWinMain) {
        si.SuppressBackgroundThread = noBgThread;
        Gdiplus::GdiplusStartup(&token, &si, &so);
        if (noBgThread)
            so.NotificationHook(&hookToken);
    }
    ~ScopedGdiPlus() {
        if (noBgThread)
            so.NotificationUnhook(hookToken);
        Gdiplus::GdiplusShutdown(token);
    }
};

template <typename T>
class ScopedGdiObj {
    T obj;
public:
    explicit ScopedGdiObj(T obj) : obj(obj) { }
    ~ScopedGdiObj() { DeleteObject(obj); }
    operator T() const { return obj; }
};
typedef ScopedGdiObj<HFONT> ScopedFont;

class ScopedCom {
public:
    ScopedCom() { CoInitialize(NULL); }
    ~ScopedCom() { CoUninitialize(); }
};

namespace geomutil {

template <typename T>
class PointT
{
public:
    T x, y;

    PointT() : x(0), y(0) { }
    PointT(T x, T y) : x(x), y(y) { }

    template <typename S>
    PointT<S> Convert() const {
        return PointT<S>((S) x, (S) y);
    }
    template <>
    PointT<int> Convert() const {
        return PointT<int>((int) floor(x + 0.5), (int) floor(y + 0.5));
    }

    bool operator==(const PointT<T>& other) const {
        return this->x == other.x && this->y == other.y;
    }
    bool operator!=(const PointT<T>& other) const {
        return !this->operator==(other);
    }
};

template <typename T>
class SizeT
{
public:
    T dx, dy;

    SizeT() : dx(0), dy(0) { }
    SizeT(T dx, T dy) : dx(dx), dy(dy) { }

    template <typename S>
    SizeT<S> Convert() const {
        return SizeT<S>((S) dx, (S) dy);
    }
    template <>
    SizeT<int> Convert() const {
        return SizeT<int>((int) floor(dx + 0.5), (int) floor(dy + 0.5));
    }

    bool IsEmpty() const {
        return dx == 0 || dy == 0;
    }

    bool operator==(const SizeT<T>& other) const {
        return this->dx == other.dx && this->dy == other.dy;
    }
    bool operator!=(const SizeT<T>& other) const {
        return !this->operator==(other);
    }
};

template <typename T>
class RectT
{
public:
    T x, y;
    T dx, dy;

    RectT() : x(0), y(0), dx(0), dy(0) { }
    RectT(T x, T y, T dx, T dy) : x(x), y(y), dx(dx), dy(dy) { }
    RectT(PointT<T> pt, SizeT<T> size) : x(pt.x), y(pt.y), dx(size.dx), dy(size.dy) { }

    static RectT FromXY(T xs, T ys, T xe, T ye) {
        if (xs > xe)
            Swap(xs, xe);
        if (ys > ye)
            Swap(ys, ye);
        return RectT(xs, ys, xe - xs, ye - ys);
    }
    static RectT FromXY(PointT<T> TL, PointT<T> BR) {
        return FromXY(TL.x, TL.y, BR.x, BR.y);
    }

    template <typename S>
    RectT<S> Convert() const {
        return RectT<S>((S) x, (S) y, (S) dx, (S) dy);
    }
    template <>
    RectT<int> Convert() const {
        return RectT<int>((int) floor(x + 0.5), (int) floor(y + 0.5),
            (int) floor(dx + 0.5), (int) floor(dy + 0.5));
    }
    // cf. fz_roundrect in mupdf/fitz/base_geometry.c
#ifndef FLT_EPSILON
#define FLT_EPSILON 1.192092896e-07f
#endif
    RectT<int> Round() const {
        return RectT<int>::FromXY((int) floor(x + FLT_EPSILON),
            (int) floor(y + FLT_EPSILON),
            (int) ceil(x + dx - FLT_EPSILON),
            (int) ceil(y + dy - FLT_EPSILON));
    }

    bool IsEmpty() const {
        return dx == 0 || dy == 0;
    }

    bool Contains(PointT<T> pt) const {
        if (pt.x < this->x)
            return false;
        if (pt.x > this->x + this->dx)
            return false;
        if (pt.y < this->y)
            return false;
        if (pt.y > this->y + this->dy)
            return false;
        return true;
    }

    /* Returns an empty rectangle if there's no intersection (see IsEmpty). */
    RectT Intersect(RectT other) const {
        /* The intersection starts with the larger of the start coordinates
        and ends with the smaller of the end coordinates */
        T x = max(this->x, other.x);
        T y = max(this->y, other.y);
        T dx = min(this->x + this->dx, other.x + other.dx) - x;
        T dy = min(this->y + this->dy, other.y + other.dy) - y;

        /* return an empty rectangle if the dimensions aren't positive */
        if (dx <= 0 || dy <= 0)
            return RectT();
        return RectT(x, y, dx, dy);
    }

    RectT Union(RectT other) const {
        if (this->dx <= 0 && this->dy <= 0)
            return other;
        if (other.dx <= 0 && other.dy <= 0)
            return *this;

        /* The union starts with the smaller of the start coordinates
        and ends with the larger of the end coordinates */
        T x = min(this->x, other.x);
        T y = min(this->y, other.y);
        T dx = max(this->x + this->dx, other.x + other.dx) - x;
        T dy = max(this->y + this->dy, other.y + other.dy) - y;

        return RectT(x, y, dx, dy);
    }

    void Offset(T _x, T _y) {
        x += _x;
        y += _y;
    }

    void Inflate(T _x, T _y) {
        x -= _x; dx += 2 * _x;
        y -= _y; dy += 2 * _y;
    }

    PointT<T> TL() const { return PointT<T>(x, y); }
    PointT<T> BR() const { return PointT<T>(x + dx, y + dy); }
    SizeT<T> Size() const { return SizeT<T>(dx, dy); }

#ifdef _WIN32
    RECT ToRECT() const {
        RectT<int> rectI(this->Convert<int>());
        RECT result = { rectI.x, rectI.y, rectI.x + rectI.dx, rectI.y + rectI.dy };
        return result;
    }
    static RectT FromRECT(const RECT& rect) {
        return FromXY(rect.left, rect.top, rect.right, rect.bottom);
    }

#ifdef GDIPVER
    Gdiplus::Rect ToGdipRect() const {
        RectT<int> rect(this->Convert<int>());
        return Gdiplus::Rect(rect.x, rect.y, rect.dx, rect.dy);
    }
    Gdiplus::RectF ToGdipRectF() const {
        RectT<float> rectF(this->Convert<float>());
        return Gdiplus::RectF(rectF.x, rectF.y, rectF.dx, rectF.dy);
    }
#endif
#endif

    bool operator==(const RectT<T>& other) const {
        return this->x == other.x && this->y == other.y &&
            this->dx == other.dx && this->dy == other.dy;
    }
    bool operator!=(const RectT<T>& other) const {
        return !this->operator==(other);
    }
};

} // namespace geomutil

typedef geomutil::RectT<int> RectI;

class ClientRect : public RectI {
public:
    explicit ClientRect(HWND hwnd) {
        RECT rc;
        if (GetClientRect(hwnd, &rc)) {
            x = rc.left; dx = rc.right - rc.left;
            y = rc.top; dy = rc.bottom - rc.top;
        }
    }
};


#endif
