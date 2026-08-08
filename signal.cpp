#include <windows.h>
#include <mmsystem.h>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <ctime>
#include <vector>
#include <string>
#include <algorithm>
#include <random>

using namespace std;

#define LG_W 960
#define LG_H 640
#define MAX_LVL 20
#define MAX_UPGRADES 12
#define MAX_RECORDS 10
#define SPR_N 16
#define SPRKEY RGB(255, 0, 255)

const double PI = 3.141592653589793;

static wchar_t g_exeDir[MAX_PATH];
static wchar_t g_soundDir[MAX_PATH];
static mt19937 g_rng;

static float rndf(float a, float b) { return a + (b - a) * (float)((g_rng() % 100000) / 100000.0); }
static int rndi(int a, int b) { return a + (int)((g_rng() % 100000) / 100000.0 * (b - a + 1)); }

static COLORREF mixC(COLORREF c, COLORREF b, float t) {
    int r = (int)((GetRValue(c)) * (1 - t) + GetRValue(b) * t);
    int g = (int)(GetGValue(c) * (1 - t) + GetGValue(b) * t);
    int bl = (int)(GetBValue(c) * (1 - t) + GetBValue(b) * t);
    return RGB(r, g, bl);
}
static COLORREF darken(COLORREF c, float t) { return mixC(c, RGB(0, 0, 0), t); }

static COLORREF C_BG = RGB(7, 9, 16);
static COLORREF C_PLAYER = RGB(0, 229, 255);
static COLORREF C_VIRUS = RGB(255, 64, 64);
static COLORREF C_BOT = RGB(255, 165, 40);
static COLORREF C_SPAM = RGB(90, 255, 120);
static COLORREF C_TANK = RGB(205, 90, 255);
static COLORREF C_PROXY = RGB(170, 170, 180);
static COLORREF C_BOSS = RGB(255, 45, 140);
static COLORREF C_ORB = RGB(255, 220, 80);
static COLORREF C_UI = RGB(215, 228, 240);
static COLORREF C_ACCENT = RGB(0, 200, 255);
static COLORREF C_RED = RGB(255, 90, 90);

static void ansiToWide(const char* src, wchar_t* dst, int dstLen);

enum EnemyType { EV_VIRUS, EV_BOTNET, EV_SPAMMER, EV_TANK, EV_PROXY, EV_BOSS };

struct Enemy { int type; float x, y, vx, vy, r, hp, maxhp, spd; int dmg, score, orbs; float shootCd, hitCd, born; };
struct PBullet { float x, y, vx, vy, r, dmg; };
struct EBullet { float x, y, vx, vy, r, dmg; };
struct Orb { float x, y, vx, vy, r; int value; int kind; };
struct Particle { float x, y, vx, vy, life, maxlife; int r; COLORREF c; };
struct FText { float x, y, life; wstring s; COLORREF c; };
struct BgLine { float x1, y1, x2, y2; COLORREF c; };
struct Record { int score, wave; char date[32]; };

static void sFillPoly(HDC h, COLORREF c, POINT* p, int n) {
    HBRUSH br = CreateSolidBrush(c);
    HPEN pen = CreatePen(PS_SOLID, 1, c);
    HGDIOBJ ob = SelectObject(h, br);
    HGDIOBJ op = SelectObject(h, pen);
    Polygon(h, p, n);
    SelectObject(h, ob);
    SelectObject(h, op);
    DeleteObject(br);
    DeleteObject(pen);
}
static void sCircle(HDC h, int x, int y, int r, COLORREF c) {
    HBRUSH br = CreateSolidBrush(c);
    HPEN pen = CreatePen(PS_SOLID, 1, c);
    HGDIOBJ ob = SelectObject(h, br);
    HGDIOBJ op = SelectObject(h, pen);
    Ellipse(h, x - r, y - r, x + r, y + r);
    SelectObject(h, ob);
    SelectObject(h, op);
    DeleteObject(br);
    DeleteObject(pen);
}
static void sRing(HDC h, int x, int y, int r, int w, COLORREF c) {
    HPEN pen = CreatePen(PS_SOLID, w, c);
    HGDIOBJ op = SelectObject(h, pen);
    HGDIOBJ ob = SelectObject(h, (HBRUSH)GetStockObject(NULL_BRUSH));
    Ellipse(h, x - r, y - r, x + r, y + r);
    SelectObject(h, op);
    SelectObject(h, ob);
    DeleteObject(pen);
}
static HBITMAP makeDib(int w, int h, DWORD key, DWORD** pixels) {
    BITMAPINFO bi;
    memset(&bi, 0, sizeof(bi));
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = w;
    bi.bmiHeader.biHeight = -h;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    void* px = NULL;
    HBITMAP bmp = CreateDIBSection(NULL, &bi, DIB_RGB_COLORS, &px, NULL, 0);
    if (px) {
        DWORD* d = (DWORD*)px;
        for (int i = 0; i < w * h; i++) d[i] = key;
        if (pixels) *pixels = d;
    }
    return bmp;
}
static DWORD bilinear(const DWORD* p, int w, int h, float fx, float fy) {
    int x0 = (int)floorf(fx), y0 = (int)floorf(fy);
    float tx = fx - x0, ty = fy - y0;
    auto get = [&](int x, int y) -> float {
        if (x < 0 || y < 0 || x >= w || y >= h) return 0;
        return p[y * w + x] == SPRKEY ? 0 : 1;
    };
    float w00 = get(x0, y0) * (1 - tx) * (1 - ty);
    float w10 = get(x0 + 1, y0) * tx * (1 - ty);
    float w01 = get(x0, y0 + 1) * (1 - tx) * ty;
    float w11 = get(x0 + 1, y0 + 1) * tx * ty;
    float a = w00 + w10 + w01 + w11;
    if (a < 0.45f) return SPRKEY;
    int r = 0, g = 0, b = 0;
    auto add = [&](int x, int y, float wt) {
        if (wt <= 0 || x < 0 || y < 0 || x >= w || y >= h) return;
        DWORD v = p[y * w + x];
        if (v == SPRKEY) return;
        r += (int)((v >> 16) & 255) * wt;
        g += (int)((v >> 8) & 255) * wt;
        b += (int)(v & 255) * wt;
    };
    add(x0, y0, w00); add(x0 + 1, y0, w10); add(x0, y0 + 1, w01); add(x0 + 1, y0 + 1, w11);
    return RGB((int)(r / a + 0.5f), (int)(g / a + 0.5f), (int)(b / a + 0.5f));
}
static void rotateFrame(const DWORD* src, int sw, int sh, DWORD* dst, int dw, int dh, double ang) {
    double cs = cos(ang), sn = sin(ang);
    double sx0 = (sw - 1) * 0.5, sy0 = (sh - 1) * 0.5;
    double cx = (dw - 1) * 0.5, cy = (dh - 1) * 0.5;
    double scale = (double)sw / dw;
    for (int oy = 0; oy < dh; oy++) {
        for (int ox = 0; ox < dw; ox++) {
            double dx = ox - cx, dy = oy - cy;
            double rx = dx * cs - dy * sn;
            double ry = dx * sn + dy * cs;
            dst[oy * dw + ox] = bilinear(src, sw, sh,
                (float)(sx0 + rx * scale), (float)(sy0 + ry * scale));
        }
    }
}
static int PX(int SF, float dx) { return (int)(128 + dx * SF); }
static int PY(int SF, float dy) { return (int)(128 + dy * SF); }
static int RP(int SF, float d) { return (int)(d * SF); }

static void artPlayer(HDC h, int SF) {
    sCircle(h, PX(SF, 0), PY(SF, 0), RP(SF, 24), darken(C_PLAYER, 0.72f));
    POINT f[3] = { {PX(SF, -17), PY(SF, -4)}, {PX(SF, -26), PY(SF, 0)}, {PX(SF, -17), PY(SF, 4)} };
    sFillPoly(h, RGB(255, 140, 40), f, 3);
    POINT f2[3] = { {PX(SF, -17), PY(SF, -2)}, {PX(SF, -22), PY(SF, 0)}, {PX(SF, -17), PY(SF, 2)} };
    sFillPoly(h, RGB(255, 230, 120), f2, 3);
    POINT w1[3] = { {PX(SF, 1), PY(SF, -8)}, {PX(SF, -11), PY(SF, -8)}, {PX(SF, -5), PY(SF, -17)} };
    sFillPoly(h, darken(C_PLAYER, 0.4f), w1, 3);
    POINT w2[3] = { {PX(SF, 1), PY(SF, 8)}, {PX(SF, -11), PY(SF, 8)}, {PX(SF, -5), PY(SF, 17)} };
    sFillPoly(h, darken(C_PLAYER, 0.4f), w2, 3);
    POINT hh[9] = {
        {PX(SF, 21), PY(SF, 0)}, {PX(SF, 13), PY(SF, -8)}, {PX(SF, 1), PY(SF, -10)},
        {PX(SF, -11), PY(SF, -8)}, {PX(SF, -16), PY(SF, -3)}, {PX(SF, -16), PY(SF, 3)},
        {PX(SF, -11), PY(SF, 8)}, {PX(SF, 1), PY(SF, 10)}, {PX(SF, 13), PY(SF, 8)}
    };
    sFillPoly(h, C_PLAYER, hh, 9);
    sRing(h, PX(SF, 0), PY(SF, 0), RP(SF, 13), RP(SF, 1), darken(C_PLAYER, 0.35f));
    sCircle(h, PX(SF, 6), PY(SF, 0), RP(SF, 4), RGB(240, 255, 255));
}

static void artVirus(HDC h, int SF) {
    sCircle(h, PX(SF, 0), PY(SF, 0), RP(SF, 22), darken(C_VIRUS, 0.72f));
    for (int i = 0; i < 10; i++) {
        float a = i * 2 * (float)PI / 10;
        POINT s[3];
        s[0] = { PX(SF, cosf(a - 0.15f) * 14), PY(SF, sinf(a - 0.15f) * 14) };
        s[1] = { PX(SF, cosf(a + 0.15f) * 14), PY(SF, sinf(a + 0.15f) * 14) };
        s[2] = { PX(SF, cosf(a) * 18), PY(SF, sinf(a) * 18) };
        sFillPoly(h, mixC(C_VIRUS, C_BG, 0.3f), s, 3);
    }
    sCircle(h, PX(SF, 0), PY(SF, 0), RP(SF, 15), C_VIRUS);
    sCircle(h, PX(SF, 0), PY(SF, 0), RP(SF, 10), darken(C_VIRUS, 0.35f));
    sCircle(h, PX(SF, 1), PY(SF, -1), RP(SF, 4), RGB(255, 170, 170));
    sRing(h, PX(SF, 0), PY(SF, 0), RP(SF, 15), RP(SF, 1.5f), darken(C_VIRUS, 0.5f));
}

static void artBotnet(HDC h, int SF) {
    sCircle(h, PX(SF, 0), PY(SF, 0), RP(SF, 18), darken(C_BOT, 0.72f));
    POINT d[4] = { {PX(SF, 0), PY(SF, -14)}, {PX(SF, 14), PY(SF, 0)}, {PX(SF, 0), PY(SF, 14)}, {PX(SF, -14), PY(SF, 0)} };
    sFillPoly(h, C_BOT, d, 4);
    POINT d2[4] = { {PX(SF, 0), PY(SF, -8)}, {PX(SF, 8), PY(SF, 0)}, {PX(SF, 0), PY(SF, 8)}, {PX(SF, -8), PY(SF, 0)} };
    sFillPoly(h, RGB(255, 205, 90), d2, 4);
    POINT v[4] = { {PX(SF, -10), PY(SF, -3)}, {PX(SF, 10), PY(SF, -3)}, {PX(SF, 8), PY(SF, 2)}, {PX(SF, -8), PY(SF, 2)} };
    sFillPoly(h, RGB(40, 24, 0), v, 4);
    sCircle(h, PX(SF, -5), PY(SF, -1), RP(SF, 1.6f), RGB(255, 220, 120));
    sCircle(h, PX(SF, 5), PY(SF, -1), RP(SF, 1.6f), RGB(255, 220, 120));
}

static void artSpammer(HDC h, int SF) {
    sCircle(h, PX(SF, 0), PY(SF, 0), RP(SF, 20), darken(C_SPAM, 0.72f));
    POINT c[4] = { {PX(SF, -4), PY(SF, -13)}, {PX(SF, 4), PY(SF, -13)}, {PX(SF, 6), PY(SF, -22)}, {PX(SF, -6), PY(SF, -22)} };
    sFillPoly(h, darken(C_SPAM, 0.25f), c, 4);
    POINT m[4] = { {PX(SF, -7), PY(SF, -24)}, {PX(SF, 7), PY(SF, -24)}, {PX(SF, 4), PY(SF, -19)}, {PX(SF, -4), PY(SF, -19)} };
    sFillPoly(h, darken(C_SPAM, 0.45f), m, 4);
    POINT b[6] = {
        {PX(SF, -12), PY(SF, -9)}, {PX(SF, 12), PY(SF, -9)}, {PX(SF, 15), PY(SF, 0)},
        {PX(SF, 12), PY(SF, 9)}, {PX(SF, -12), PY(SF, 9)}, {PX(SF, -15), PY(SF, 0)}
    };
    sFillPoly(h, C_SPAM, b, 6);
    sCircle(h, PX(SF, 0), PY(SF, 3), RP(SF, 7), darken(C_SPAM, 0.2f));
    sCircle(h, PX(SF, 0), PY(SF, 3), RP(SF, 4), RGB(255, 40, 40));
    sCircle(h, PX(SF, -1), PY(SF, 2), RP(SF, 1.5f), RGB(255, 180, 180));
}

static void artTank(HDC h, int SF) {
    sCircle(h, PX(SF, 0), PY(SF, 0), RP(SF, 28), darken(C_TANK, 0.72f));
    POINT hx[6];
    for (int i = 0; i < 6; i++) {
        float a = i * (float)PI / 3;
        hx[i] = { PX(SF, cosf(a) * 24), PY(SF, sinf(a) * 24) };
    }
    sFillPoly(h, C_TANK, hx, 6);
    POINT hx2[6];
    for (int i = 0; i < 6; i++) {
        float a = i * (float)PI / 3 + (float)PI / 6;
        hx2[i] = { PX(SF, cosf(a) * 15), PY(SF, sinf(a) * 15) };
    }
    sFillPoly(h, mixC(C_TANK, C_BG, 0.25f), hx2, 6);
    sCircle(h, PX(SF, 0), PY(SF, 0), RP(SF, 8), darken(C_TANK, 0.45f));
    for (int i = 0; i < 3; i++) {
        float a = i * (float)PI / 3 + (float)PI / 6;
        HPEN pen = CreatePen(PS_SOLID, RP(SF, 2), darken(C_TANK, 0.5f));
        HGDIOBJ op = SelectObject(h, pen);
        MoveToEx(h, PX(SF, cosf(a) * 13), PY(SF, sinf(a) * 13), 0);
        LineTo(h, PX(SF, cosf(a) * 23), PY(SF, sinf(a) * 23));
        SelectObject(h, op);
        DeleteObject(pen);
    }
    POINT e1[3] = { {PX(SF, -8), PY(SF, -6)}, {PX(SF, -2), PY(SF, -2)}, {PX(SF, -8), PY(SF, -1)} };
    POINT e2[3] = { {PX(SF, 8), PY(SF, -6)}, {PX(SF, 2), PY(SF, -2)}, {PX(SF, 8), PY(SF, -1)} };
    sFillPoly(h, RGB(255, 80, 120), e1, 3);
    sFillPoly(h, RGB(255, 80, 120), e2, 3);
}

static void artProxy(HDC h, int SF) {
    sCircle(h, PX(SF, 0), PY(SF, 0), RP(SF, 18), darken(C_PROXY, 0.72f));
    sCircle(h, PX(SF, 0), PY(SF, 0), RP(SF, 13), C_PROXY);
    POINT ha[5] = { {PX(SF, -13), PY(SF, 0)}, {PX(SF, 13), PY(SF, 0)}, {PX(SF, 11), PY(SF, -11)}, {PX(SF, 0), PY(SF, -14)}, {PX(SF, -11), PY(SF, -11)} };
    sFillPoly(h, darken(C_PROXY, 0.25f), ha, 5);
    POINT bw[4] = { {PX(SF, -13), PY(SF, -2)}, {PX(SF, 13), PY(SF, -2)}, {PX(SF, 12), PY(SF, 4)}, {PX(SF, -12), PY(SF, 4)} };
    sFillPoly(h, RGB(240, 200, 60), bw, 4);
    POINT st1[4] = { {PX(SF, -8), PY(SF, -2)}, {PX(SF, -3), PY(SF, -2)}, {PX(SF, -6), PY(SF, 4)}, {PX(SF, -10), PY(SF, 4)} };
    POINT st2[4] = { {PX(SF, 3), PY(SF, -2)}, {PX(SF, 8), PY(SF, -2)}, {PX(SF, 6), PY(SF, 4)}, {PX(SF, 1), PY(SF, 4)} };
    sFillPoly(h, RGB(40, 30, 0), st1, 4);
    sFillPoly(h, RGB(40, 30, 0), st2, 4);
    sCircle(h, PX(SF, 0), PY(SF, -6), RP(SF, 3.5f), RGB(255, 60, 40));
    sCircle(h, PX(SF, -1), PY(SF, -7), RP(SF, 1.2f), RGB(255, 200, 180));
    sCircle(h, PX(SF, 12), PY(SF, -12), RP(SF, 4.5f), darken(C_PROXY, 0.4f));
    HPEN pen = CreatePen(PS_SOLID, RP(SF, 1.5f), darken(C_PROXY, 0.5f));
    HGDIOBJ op = SelectObject(h, pen);
    MoveToEx(h, PX(SF, 15), PY(SF, -15), 0);
    LineTo(h, PX(SF, 18), PY(SF, -18));
    SelectObject(h, op);
    DeleteObject(pen);
    sCircle(h, PX(SF, 19), PY(SF, -19), RP(SF, 2.2f), RGB(255, 240, 120));
}

static void artBoss(HDC h, int SF) {
    sCircle(h, PX(SF, 0), PY(SF, 0), RP(SF, 42), darken(C_BOSS, 0.72f));
    for (int i = 0; i < 8; i++) {
        float a = i * (float)PI / 4;
        POINT t[4];
        float wb = 5.5f, wt = 1.8f;
        t[0] = { PX(SF, cosf(a) * 20 + cosf(a + (float)PI / 2) * wb), PY(SF, sinf(a) * 20 + sinf(a + (float)PI / 2) * wb) };
        t[1] = { PX(SF, cosf(a) * 20 - cosf(a + (float)PI / 2) * wb), PY(SF, sinf(a) * 20 - sinf(a + (float)PI / 2) * wb) };
        t[2] = { PX(SF, cosf(a) * 42 - cosf(a + (float)PI / 2) * wt), PY(SF, sinf(a) * 42 - sinf(a + (float)PI / 2) * wt) };
        t[3] = { PX(SF, cosf(a) * 42 + cosf(a + (float)PI / 2) * wt), PY(SF, sinf(a) * 42 + sinf(a + (float)PI / 2) * wt) };
        sFillPoly(h, darken(C_BOSS, i % 2 ? 0.45f : 0.25f), t, 4);
    }
    POINT star[10];
    for (int i = 0; i < 10; i++) {
        float a = i * (float)PI / 5;
        float r = i % 2 ? 20 : 28;
        star[i] = { PX(SF, cosf(a) * r), PY(SF, sinf(a) * r) };
    }
    sFillPoly(h, C_BOSS, star, 10);
    sCircle(h, PX(SF, 0), PY(SF, 0), RP(SF, 20), darken(C_BOSS, 0.3f));
    sCircle(h, PX(SF, 0), PY(SF, 0), RP(SF, 9), RGB(20, 4, 14));
    sCircle(h, PX(SF, 1.5f), PY(SF, -1), RP(SF, 5), RGB(255, 60, 90));
    sCircle(h, PX(SF, 3), PY(SF, -2.5f), RP(SF, 1.6f), RGB(255, 220, 220));
    for (int i = -1; i <= 1; i++) {
        POINT t[3] = { {PX(SF, i * 4 - 1.5f), PY(SF, 9)}, {PX(SF, i * 4 + 1.5f), PY(SF, 9)}, {PX(SF, i * 4), PY(SF, 15)} };
        sFillPoly(h, RGB(255, 220, 230), t, 3);
    }
}

static void artSprite(HDC h, int type, int SF) {
    switch (type) {
        case 0: artPlayer(h, SF); break;
        case 1: artVirus(h, SF); break;
        case 2: artBotnet(h, SF); break;
        case 3: artSpammer(h, SF); break;
        case 4: artTank(h, SF); break;
        case 5: artProxy(h, SF); break;
        case 6: artBoss(h, SF); break;
    }
}

struct Sprite {
    int fw, fh, sw, sh;
    HBITMAP srcBmp;
    DWORD* srcPix;
    vector<HBITMAP> frames;
    vector<HDC> dcs;
};

struct Game {
    HWND hwnd;
    HDC bufDC;
    HBITMAP bufBmp;
    HFONT fTiny, fSmall, fMid, fBig, fTitle;
    int cw, ch;
    RECT winRect;
    bool fullscreen;

    bool kb[256], kp[256];
    int mx, my;
    bool mDown, mClick;

    int state;
    int menuSel, howPage, settingsSel, pauseSel;
    bool newRec;

    int difficulty, volume;
    bool soundOn, autoaim;

    float px, py, pa, pvx, pvy;
    float hp, maxhp, shield;
    int lvl;
    float xp, xpNext;
    int score, kills;
    float elapsed;
    int wave;
    float waveTimer;
    int waveState, budget, spawned;
    float spawnTimer;
    float fireTimer, dashCd, abilCd, slowCd, slowTimer, dashTimer, invuln, autobombCd;
    float shake, flash;
    int up[MAX_UPGRADES];
    int consum[3];
    bool dashDirSet; float dashDx, dashDy;

    vector<Enemy> en;
    vector<PBullet> pb;
    vector<EBullet> eb;
    vector<Orb> orbs;
    vector<Particle> part;
    vector<FText> ftext;
    vector<BgLine> bg;
    int choices[3];
    RECT cardR[3];
    vector<Record> rec;
    Sprite sp[7];

    void buildSprites();
    int frameIndex(float ang);
    void drawSprite(HDC h, int type, float x, float y, float ang, float scale);

    void init(HWND w);
    void resetGame();
    void startWave();
    void spawnEnemy(int type);
    void damagePlayer(int d);
    void killEnemy(int idx, bool overload, bool explode);
    void explodeProxy(int idx);
    void addScore(int v);
    void gainXp(int v);
    void makeLevelChoices();
    void applyUpgrade(int id);
    void shoot();
    void fireAbility();
    void slowAbility();
    void useConsum(int k);
    void autoBomb();
    void addParticles(float x, float y, COLORREF c, int n, float spd);
    void addText(float x, float y, const wchar_t* s, COLORREF c);
    void update(float dt);
    void render();
    void drawCircle(HDC h, float x, float y, float r, COLORREF c, bool fill);
    void drawShape(HDC h, const Enemy& e);
    void drawPlayer(HDC h);
    void drawMenu(HDC h);
    void drawHowto(HDC h);
    void drawScores(HDC h);
    void drawSettings(HDC h);
    void drawHUD(HDC h);
    void drawLevelUp(HDC h);
    void drawPause(HDC h);
    void drawGameOver(HDC h);
    void drawBar(HDC h, float x, float y, float w, float hh, float frac, COLORREF c);
    void text(HDC h, float x, float y, float w, float hh, const wchar_t* s, COLORREF c, HFONT f, bool ch = true, bool cv = true);
    void applyFullscreen(bool fs);
    void saveSettings();
    void loadSettings();
    void loadRecords();
    void saveRecords();
    void recordScore();
    void buildBg();
    void genAllSounds();
    void playSnd(const wchar_t* name);
};

static Game G;

static const wchar_t* UPG_NAME[MAX_UPGRADES] = {
    L"Скорострельность", L"Урон", L"Скорость", L"Двойной выстрел", L"Веер",
    L"Щит", L"Регенерация", L"Вампиризм", L"Магнит", L"Замедление пуль",
    L"Броня", L"Авто-бомба"
};
static const wchar_t* UPG_DESC[MAX_UPGRADES] = {
    L"+20% скорострельности", L"+25% урона", L"+10% скорости",
    L"+1 дополнительный снаряд", L"+2 боковых снаряда", L"+30 щита",
    L"+1 HP/сек", L"5% шанс лечиться за убийство", L"+40% радиус сбора",
    L"-20% скорости вражеских снарядов", L"-15% входящего урона",
    L"Взрыв по площади при уроне"
};
static const int UPG_MAX[MAX_UPGRADES] = { 5, 5, 5, 3, 3, 3, 3, 3, 3, 2, 3, 2 };

static const wchar_t* MENU_ITEMS[5] = { L"Играть", L"Как играть", L"Рекорды", L"Настройки", L"Выход" };
static const wchar_t* PAUSE_ITEMS[4] = { L"Продолжить", L"В меню", L"Настройки", L"Выход" };

void Game::init(HWND w) {
    hwnd = w;
    cw = LG_W; ch = LG_H;
    fullscreen = false;
    memset(kb, 0, sizeof(kb));
    memset(kp, 0, sizeof(kp));
    mDown = mClick = false;
    state = 0;
    menuSel = 0; howPage = 0; settingsSel = 0; pauseSel = 0;
    loadSettings();
    loadRecords();
    buildBg();
    resetGame();
    state = 0;
    genAllSounds();
    buildSprites();
}

void Game::buildBg() {
    bg.clear();
    for (int i = 0; i < 40; i++) {
        BgLine l;
        l.x1 = rndf(0, LG_W); l.y1 = rndf(0, LG_H);
        float ang = rndf(0, (float)PI * 2);
        float len = rndf(20, 120);
        l.x2 = l.x1 + cosf(ang) * len; l.y2 = l.y1 + sinf(ang) * len;
        int k = rndi(0, 2);
        l.c = k == 0 ? RGB(20, 40, 70) : (k == 1 ? RGB(15, 35, 60) : RGB(25, 45, 75));
        bg.push_back(l);
    }
}

void Game::resetGame() {
    px = LG_W / 2.0f; py = LG_H / 2.0f; pa = 0; pvx = pvy = 0;
    maxhp = 100; hp = 100; shield = 0;
    lvl = 0; xp = 0; xpNext = 4;
    score = 0; kills = 0; elapsed = 0;
    wave = 0; waveState = 0; waveTimer = 2.0f; budget = 0; spawned = 0; spawnTimer = 0;
    fireTimer = 0; dashCd = 0; abilCd = 0; slowCd = 0; slowTimer = 0; dashTimer = 0; invuln = 0; autobombCd = 0;
    shake = 0; flash = 0;
    for (int i = 0; i < MAX_UPGRADES; i++) up[i] = 0;
    consum[0] = 1; consum[1] = 0; consum[2] = 0;
    dashDirSet = false; dashDx = 0; dashDy = 0;
    en.clear(); pb.clear(); eb.clear(); orbs.clear(); part.clear(); ftext.clear();
    newRec = false;
}

void Game::startWave() {
    wave++;
    budget = 0;
    int w = wave;
    if (w % 5 == 0) {
        spawnEnemy(EV_BOSS);
    }
    budget = (int)((5 + w * 2) * (difficulty == 0 ? 0.85f : (difficulty == 1 ? 1.0f : 1.3f)));
    spawned = 0;
}

void Game::spawnEnemy(int type) {
    Enemy e;
    int side = rndi(0, 3);
    float x = 0, y = 0;
    if (side == 0) { x = rndf(-30, LG_W + 30); y = -30; }
    else if (side == 1) { x = rndf(-30, LG_W + 30); y = LG_H + 30; }
    else if (side == 2) { x = -30; y = rndf(-30, LG_H + 30); }
    else { x = LG_W + 30; y = rndf(-30, LG_H + 30); }
    e.x = x; e.y = y; e.vx = 0; e.vy = 0;
    e.hitCd = 0; e.born = 0.3f;
    float mult = 1 + (wave - 1) * 0.07f;
    float hpm = difficulty == 0 ? 0.8f : (difficulty == 1 ? 1.0f : 1.25f);
    float spm = difficulty == 0 ? 0.9f : (difficulty == 1 ? 1.0f : 1.15f);
    e.type = type;
    switch (type) {
        case EV_VIRUS: e.r = 12; e.hp = e.maxhp = 1 * mult * hpm; e.spd = 62 * spm; e.dmg = 10; e.score = 10; e.orbs = 1; e.shootCd = 0; break;
        case EV_BOTNET: e.r = 8; e.hp = e.maxhp = 1 * mult * hpm; e.spd = 135 * spm; e.dmg = 6; e.score = 15; e.orbs = 1; e.shootCd = 0; break;
        case EV_SPAMMER: e.r = 14; e.hp = e.maxhp = 3 * mult * hpm; e.spd = 40 * spm; e.dmg = 8; e.score = 25; e.orbs = 2; e.shootCd = 1.2f; break;
        case EV_TANK: e.r = 22; e.hp = e.maxhp = 8 * mult * hpm; e.spd = 26 * spm; e.dmg = 20; e.score = 50; e.orbs = 3; e.shootCd = 0; break;
        case EV_PROXY: e.r = 11; e.hp = e.maxhp = 2 * mult * hpm; e.spd = 112 * spm; e.dmg = 25; e.score = 30; e.orbs = 2; e.shootCd = 0; break;
        case EV_BOSS: e.r = 42; e.hp = e.maxhp = 60 * (1 + (wave - 1) * 0.15f) * hpm; e.spd = 46 * spm; e.dmg = 30; e.score = 500; e.orbs = 15; e.shootCd = 1.0f; break;
    }
    en.push_back(e);
}

int pickEnemyType(int w) {
    vector<int> wts;
    wts.push_back(100);
    wts.push_back(w >= 2 ? 60 : 0);
    wts.push_back(w >= 3 ? 42 : 0);
    wts.push_back(w >= 4 ? 32 : 0);
    wts.push_back(w >= 5 ? 38 : 0);
    int total = 0;
    for (int v : wts) total += v;
    int r = rndi(0, total - 1);
    for (int i = 0; i < 5; i++) { if (r < wts[i]) return i; r -= wts[i]; }
    return 0;
}

void Game::shoot() {
    float aim = pa;
    float baseSpd = 520;
    float dmg = 10 * (1 + 0.25f * up[1]) * (difficulty == 0 ? 1.2f : (difficulty == 1 ? 1.0f : 0.85f));
    int shots = 1 + up[3];
    int fan = up[4];
    for (int i = 0; i < shots; i++) {
        float off = 0;
        if (shots > 1) off = ((float)i - (shots - 1) / 2.0f) * 0.12f;
        PBullet b;
        b.r = 4; b.dmg = dmg;
        b.vx = cosf(aim + off) * baseSpd; b.vy = sinf(aim + off) * baseSpd;
        b.x = px + cosf(aim + off) * 18; b.y = py + sinf(aim + off) * 18;
        pb.push_back(b);
    }
    for (int i = 1; i <= fan; i++) {
        for (int s = -1; s <= 1; s += 2) {
            float off = i * 0.18f * s;
            PBullet b;
            b.r = 4; b.dmg = dmg;
            b.vx = cosf(aim + off) * baseSpd; b.vy = sinf(aim + off) * baseSpd;
            b.x = px + cosf(aim + off) * 18; b.y = py + sinf(aim + off) * 18;
            pb.push_back(b);
        }
    }
}

void Game::addParticles(float x, float y, COLORREF c, int n, float spd) {
    for (int i = 0; i < n; i++) {
        Particle p;
        p.x = x; p.y = y;
        float a = rndf(0, (float)PI * 2);
        float s = rndf(20, spd);
        p.vx = cosf(a) * s; p.vy = sinf(a) * s;
        p.life = p.maxlife = rndf(0.25f, 0.7f);
        p.r = rndi(1, 3);
        p.c = c;
        part.push_back(p);
    }
}

void Game::addText(float x, float y, const wchar_t* s, COLORREF c) {
    FText t; t.x = x; t.y = y; t.life = 1.0f; t.s = s; t.c = c;
    ftext.push_back(t);
}

void Game::addScore(int v) {
    float m = difficulty == 2 ? 1.5f : 1.0f;
    score += (int)(v * m);
}

void Game::gainXp(int v) {
    xp += v;
    xpNext = 4 + (lvl - 1) * 2;
    if (lvl >= MAX_LVL) { xp = 0; return; }
    if (xp >= xpNext) {
        xp -= xpNext;
        lvl++;
        makeLevelChoices();
        if (choices[0] < 0 && choices[1] < 0 && choices[2] < 0) { xp = 0; return; }
        memset(kp, 0, sizeof(kp));
        mClick = false;
        playSnd(L"levelup");
        state = 5;
    }
}

void Game::makeLevelChoices() {
    vector<int> pool;
    for (int i = 0; i < MAX_UPGRADES; i++) if (up[i] < UPG_MAX[i]) pool.push_back(i);
    if (pool.empty()) { choices[0] = choices[1] = choices[2] = -1; return; }
    shuffle(pool.begin(), pool.end(), g_rng);
    for (int i = 0; i < 3; i++) choices[i] = i < (int)pool.size() ? pool[i] : -1;
}

void Game::applyUpgrade(int id) {
    if (id < 0) return;
    up[id]++;
    switch (id) {
        case 5: shield += 30; if (shield > 150) shield = 150; break;
        default: break;
    }
}

void Game::damagePlayer(int d) {
    if (invuln > 0) return;
    int dmg = (int)(d * (1 - 0.15f * up[10]));
    if (shield > 0) {
        float ab = min(shield, (float)dmg);
        shield -= ab; dmg -= (int)ab;
    }
    if (dmg <= 0) dmg = 1;
    hp -= dmg;
    flash = 0.5f; shake += 7;
    playSnd(L"hurt");
    if (autobombCd <= 0 && up[11] > 0) {
        autobombCd = 10;
        autoBomb();
    }
    if (hp <= 0) {
        hp = 0;
        addParticles(px, py, C_PLAYER, 40, 260);
        playSnd(L"die");
        shake += 20;
        recordScore();
        state = 7;
    }
}

void Game::autoBomb() {
    for (int i = (int)en.size() - 1; i >= 0; i--) {
        float dx = en[i].x - px, dy = en[i].y - py;
        if (dx * dx + dy * dy < 220 * 220) {
            en[i].hp -= 40;
            if (en[i].hp <= 0) killEnemy(i, false, en[i].type == EV_PROXY);
        }
    }
    for (int i = (int)eb.size() - 1; i >= 0; i--) {
        float dx = eb[i].x - px, dy = eb[i].y - py;
        if (dx * dx + dy * dy < 260 * 260) eb.erase(eb.begin() + i);
    }
    addParticles(px, py, C_ACCENT, 30, 300);
    playSnd(L"explode");
}

void Game::killEnemy(int idx, bool overload, bool explode) {
    Enemy& e = en[idx];
    addScore(e.score);
    kills++;
    COLORREF c;
    switch (e.type) {
        case EV_VIRUS: c = C_VIRUS; break;
        case EV_BOTNET: c = C_BOT; break;
        case EV_SPAMMER: c = C_SPAM; break;
        case EV_TANK: c = C_TANK; break;
        case EV_PROXY: c = C_PROXY; break;
        default: c = C_BOSS; break;
    }
    addParticles(e.x, e.y, c, e.type == EV_BOSS ? 40 : 14, e.type == EV_BOSS ? 320 : 180);
    if (e.type == EV_BOSS) shake += 14; else shake += 2.5f;
    playSnd(e.type == EV_BOSS ? L"explode" : (e.type == EV_TANK ? L"explode" : L"hit"));
    int orbN = e.orbs;
    for (int i = 0; i < orbN; i++) {
        Orb o;
        o.x = e.x; o.y = e.y;
        float a = rndf(0, (float)PI * 2);
        float s = rndf(30, 90);
        o.vx = cosf(a) * s; o.vy = sinf(a) * s;
        o.r = e.type == EV_BOSS ? 7 : 5; o.value = 1; o.kind = 0;
        orbs.push_back(o);
    }
    if (!overload && rndf(0, 1) < 0.05f) {
        Orb o;
        o.x = e.x; o.y = e.y;
        o.vx = rndf(-60, 60); o.vy = rndf(-60, 60);
        o.r = 7;
        o.kind = rndi(1, 3);
        o.value = 0;
        orbs.push_back(o);
    }
    addText(e.x, e.y, (wstring(L"+") + to_wstring(e.score)).c_str(), C_ORB);
    if (rndf(0, 1) < 0.05f * up[7]) { hp = min(maxhp, hp + 1); }
    en.erase(en.begin() + idx);
}

void Game::explodeProxy(int idx) {
    Enemy& e = en[idx];
    float dx = e.x - px, dy = e.y - py;
    float dd = sqrtf(dx * dx + dy * dy);
    if (dd < 70) damagePlayer(e.dmg);
    addParticles(e.x, e.y, C_PROXY, 24, 240);
    playSnd(L"explode");
    en.erase(en.begin() + idx);
}

void Game::fireAbility() {
    if (abilCd > 0) return;
    abilCd = 15;
    playSnd(L"ability");
    addParticles(px, py, C_ACCENT, 40, 400);
    for (int i = (int)en.size() - 1; i >= 0; i--) {
        Enemy& e = en[i];
        if (e.type == EV_BOSS) {
            e.hp -= e.maxhp * 0.25f;
            if (e.hp <= 0) killEnemy(i, true, false);
        } else {
            killEnemy(i, true, e.type == EV_PROXY);
        }
    }
}

void Game::slowAbility() {
    if (slowCd > 0) return;
    slowCd = 20;
    slowTimer = 3;
    playSnd(L"ability");
}

void Game::useConsum(int k) {
    if (k < 0 || k > 2 || consum[k] <= 0) return;
    consum[k]--;
    if (k == 0) { hp = min(maxhp, hp + 40); playSnd(L"pickup"); addParticles(px, py, RGB(255, 120, 120), 12, 140); }
    else if (k == 1) { shield = min(150.0f, shield + 40); playSnd(L"pickup"); addParticles(px, py, C_ACCENT, 12, 140); }
    else {
        playSnd(L"explode");
        addParticles(px, py, C_ORB, 20, 220);
        for (int i = (int)en.size() - 1; i >= 0; i--) {
            float dx = en[i].x - px, dy = en[i].y - py;
            if (dx * dx + dy * dy < 220 * 220) {
                en[i].hp -= 40;
                if (en[i].hp <= 0) killEnemy(i, false, en[i].type == EV_PROXY);
            }
        }
        for (int i = (int)eb.size() - 1; i >= 0; i--) {
            float dx = eb[i].x - px, dy = eb[i].y - py;
            if (dx * dx + dy * dy < 260 * 260) eb.erase(eb.begin() + i);
        }
    }
}

void Game::update(float dt) {
    if (kp['M']) { soundOn = !soundOn; saveSettings(); }
    if (kp['F']) { applyFullscreen(!fullscreen); }

    if (state == 0) {
        if (kp[VK_UP] || kp['W']) { menuSel = (menuSel + 4) % 5; }
        if (kp[VK_DOWN] || kp['S']) { menuSel = (menuSel + 1) % 5; }
        {
            int yy = 260;
            for (int i = 0; i < 5; i++) {
                int top = yy + i * 52 - 14, bot = yy + i * 52 + 26;
                if (mx >= LG_W / 2 - 170 && mx <= LG_W / 2 + 170 && my >= top && my <= bot) menuSel = i;
            }
        }
        bool ok = kp[VK_RETURN] || kp[VK_SPACE] || mClick;
        if (ok) {
            if (menuSel == 0) { resetGame(); state = 4; }
            else if (menuSel == 1) { howPage = 0; state = 1; }
            else if (menuSel == 2) { state = 2; }
            else if (menuSel == 3) { settingsSel = 0; state = 3; }
            else { PostMessageW(hwnd, WM_CLOSE, 0, 0); }
        }
        if (kp[VK_ESCAPE]) { PostMessageW(hwnd, WM_CLOSE, 0, 0); }
        return;
    }
    if (state == 1) {
        if (kp[VK_LEFT] || kp['A']) { howPage = (howPage + 3) % 4; }
        if (kp[VK_RIGHT] || kp['D']) { howPage = (howPage + 1) % 4; }
        if (kp['1']) howPage = 0;
        if (kp['2']) howPage = 1;
        if (kp['3']) howPage = 2;
        if (kp['4']) howPage = 3;
        if (kp[VK_ESCAPE] || kp[VK_RETURN]) { state = 0; }
        return;
    }
    if (state == 2) {
        if (kp['X']) { rec.clear(); saveRecords(); }
        if (kp[VK_ESCAPE] || kp[VK_RETURN]) { state = 0; }
        return;
    }
    if (state == 3) {
        if (kp[VK_UP] || kp['W']) { settingsSel = (settingsSel + 4) % 5; }
        if (kp[VK_DOWN] || kp['S']) { settingsSel = (settingsSel + 1) % 5; }
        bool chL = kp[VK_LEFT] || kp['A'];
        bool chR = kp[VK_RIGHT] || kp['D'];
        if (chL || chR) {
            int dir = chR ? 1 : -1;
            if (settingsSel == 0) { difficulty = (difficulty + dir + 3) % 3; }
            else if (settingsSel == 1) { volume = max(0, min(100, volume + dir * 10)); genAllSounds(); }
            else if (settingsSel == 2) { soundOn = !soundOn; }
            else if (settingsSel == 3) { applyFullscreen(!fullscreen); }
            else if (settingsSel == 4) { autoaim = !autoaim; }
            saveSettings();
        }
        if (kp[VK_ESCAPE] || kp[VK_RETURN]) { saveSettings(); state = 0; }
        return;
    }
    if (state == 7) {
        if (kp['R']) { resetGame(); state = 4; }
        if (kp[VK_RETURN]) { state = 0; }
        if (kp[VK_ESCAPE]) { PostMessageW(hwnd, WM_CLOSE, 0, 0); }
        return;
    }
    if (state == 5) {
        if (kp['1']) { applyUpgrade(choices[0]); state = 4; }
        else if (kp['2']) { applyUpgrade(choices[1]); state = 4; }
        else if (kp['3']) { applyUpgrade(choices[2]); state = 4; }
        else if (mClick) {
            int sel = -1;
            for (int i = 0; i < 3; i++) {
                if (mx >= cardR[i].left && mx <= cardR[i].right && my >= cardR[i].top && my <= cardR[i].bottom) sel = i;
            }
            if (sel >= 0) { applyUpgrade(choices[sel]); state = 4; }
        }
        else if (kp[VK_ESCAPE]) { state = 4; }
        return;
    }
    if (state == 6) {
        if (kp[VK_UP] || kp['W']) { pauseSel = (pauseSel + 3) % 4; }
        if (kp[VK_DOWN] || kp['S']) { pauseSel = (pauseSel + 1) % 4; }
        if (kp[VK_ESCAPE] || kp['P'] || (kp[VK_RETURN] && pauseSel == 0)) { state = 4; }
        else if (kp[VK_RETURN]) {
            if (pauseSel == 1) { state = 0; }
            else if (pauseSel == 2) { settingsSel = 0; state = 3; }
            else if (pauseSel == 3) { PostMessageW(hwnd, WM_CLOSE, 0, 0); }
        }
        return;
    }

    if (state == 4) {
        if (kp[VK_ESCAPE] || kp['P']) { pauseSel = 0; state = 6; return; }
        elapsed += dt;
        if (slowTimer > 0) { slowTimer -= dt; }

        if (kp[VK_SPACE] && dashCd <= 0) {
            dashCd = 2; dashTimer = 0.18f; invuln = 0.25f;
            float dx = (kb[VK_RIGHT] || kb['D'] ? 1 : 0) - (kb[VK_LEFT] || kb['A'] ? 1 : 0);
            float dy = (kb[VK_DOWN] || kb['S'] ? 1 : 0) - (kb[VK_UP] || kb['W'] ? 1 : 0);
            if (dx == 0 && dy == 0) { dx = cosf(pa); dy = sinf(pa); }
            float m = sqrtf(dx * dx + dy * dy);
            dashDx = dx / m; dashDy = dy / m;
            dashDirSet = true;
            playSnd(L"dash");
        }
        if (kp['E']) fireAbility();
        if (kp['Q']) slowAbility();
        if (kp['1']) useConsum(0);
        if (kp['2']) useConsum(1);
        if (kp['3']) useConsum(2);

        float dx = (kb[VK_RIGHT] || kb['D'] ? 1 : 0) - (kb[VK_LEFT] || kb['A'] ? 1 : 0);
        float dy = (kb[VK_DOWN] || kb['S'] ? 1 : 0) - (kb[VK_UP] || kb['W'] ? 1 : 0);
        if (dx != 0 || dy != 0) {
            float m = sqrtf(dx * dx + dy * dy);
            dx /= m; dy /= m;
        }
        float speed = 220 * (1 + 0.10f * up[2]);
        if (dashTimer > 0) {
            dashTimer -= dt;
            pvx = dashDx * 780; pvy = dashDy * 780;
        } else {
            pvx = dx * speed; pvy = dy * speed;
        }
        px += pvx * dt; py += pvy * dt;
        px = max(22.0f, min(LG_W - 22.0f, px));
        py = max(22.0f, min(LG_H - 22.0f, py));

        if (autoaim) {
            float bd = 1e9f; int bi = -1;
            for (int i = 0; i < (int)en.size(); i++) {
                float d = (en[i].x - px) * (en[i].x - px) + (en[i].y - py) * (en[i].y - py);
                if (d < bd) { bd = d; bi = i; }
            }
            if (bi >= 0) pa = atan2f(en[bi].y - py, en[bi].x - px);
            else pa = atan2f((float)my - py, (float)mx - px);
        } else {
            pa = atan2f((float)my - py, (float)mx - px);
        }

        if (mDown) {
            fireTimer -= dt;
            float fr = 5 * (1 + 0.20f * up[0]);
            if (fireTimer <= 0) { shoot(); fireTimer = 1.0f / fr; playSnd(L"shot"); }
        }
        dashCd = max(0.0f, dashCd - dt);
        abilCd = max(0.0f, abilCd - dt);
        slowCd = max(0.0f, slowCd - dt);
        autobombCd = max(0.0f, autobombCd - dt);
        invuln = max(0.0f, invuln - dt);
        if (up[6] > 0) hp = min(maxhp, hp + up[6] * dt);
        score += (int)(dt * 10 * (difficulty == 2 ? 1.5f : 1.0f));

        float et = dt;
        if (slowTimer > 0) et = dt * 0.4f;

        for (int i = (int)pb.size() - 1; i >= 0; i--) {
            pb[i].x += pb[i].vx * dt;
            pb[i].y += pb[i].vy * dt;
            if (pb[i].x < -40 || pb[i].x > LG_W + 40 || pb[i].y < -40 || pb[i].y > LG_H + 40) pb.erase(pb.begin() + i);
        }

        for (int i = (int)en.size() - 1; i >= 0; i--) {
            Enemy& e = en[i];
            if (e.born > 0) e.born -= dt;
            if (e.hitCd > 0) e.hitCd -= dt;
            float dx2 = px - e.x, dy2 = py - e.y;
            float d2 = sqrtf(dx2 * dx2 + dy2 * dy2);
            if (d2 > 0.01f) { e.vx = dx2 / d2 * e.spd; e.vy = dy2 / d2 * e.spd; }
            e.x += e.vx * et; e.y += e.vy * et;
            if (e.type == EV_SPAMMER || e.type == EV_BOSS) {
                e.shootCd -= et;
                if (e.shootCd <= 0 && d2 < 520) {
                    e.shootCd = e.type == EV_BOSS ? (1.0f / (1 + (wave - 1) * 0.05f)) : 1.6f;
                    int fanN = e.type == EV_BOSS ? 5 : 1;
                    float base = atan2f(py - e.y, px - e.x);
                    float bs = 210 * (1 + wave * 0.02f);
                    for (int f = 0; f < fanN; f++) {
                        float ang = base + ((float)f - (fanN - 1) / 2.0f) * 0.22f;
                        EBullet b;
                        b.r = 5; b.dmg = e.dmg;
                        b.vx = cosf(ang) * bs; b.vy = sinf(ang) * bs;
                        b.x = e.x + cosf(ang) * e.r; b.y = e.y + sinf(ang) * e.r;
                        eb.push_back(b);
                    }
                }
            }
            if (e.type == EV_PROXY && d2 < 46) { explodeProxy(i); continue; }
            if (d2 < e.r + 13 && e.hitCd <= 0 && e.born <= 0) {
                damagePlayer(e.dmg);
                e.hitCd = 0.8f;
            }
        }

        for (int i = (int)eb.size() - 1; i >= 0; i--) {
            eb[i].x += eb[i].vx * et;
            eb[i].y += eb[i].vy * et;
            if (eb[i].x < -40 || eb[i].x > LG_W + 40 || eb[i].y < -40 || eb[i].y > LG_H + 40) eb.erase(eb.begin() + i);
        }

        float mag = 90 * (1 + 0.4f * up[8]);
        for (int i = (int)orbs.size() - 1; i >= 0; i--) {
            Orb& o = orbs[i];
            float dx2 = px - o.x, dy2 = py - o.y;
            float d2 = sqrtf(dx2 * dx2 + dy2 * dy2);
            if (d2 < mag && d2 > 0.01f) { o.vx += dx2 / d2 * 600 * dt; o.vy += dy2 / d2 * 600 * dt; }
            o.vx *= 0.92f; o.vy *= 0.92f;
            o.x += o.vx * dt; o.y += o.vy * dt;
            if (d2 < o.r + 14) {
                if (o.kind == 0) {
                    gainXp(o.value);
                    addText(o.x, o.y, L"+1", C_ORB);
                    playSnd(L"pickup");
                } else {
                    if (consum[o.kind - 1] < 3) {
                        consum[o.kind - 1]++;
                        playSnd(L"pickup");
                    } else continue;
                }
                addParticles(o.x, o.y, o.kind == 0 ? C_ORB : C_ACCENT, 6, 100);
                orbs.erase(orbs.begin() + i);
            }
        }

        for (int i = (int)pb.size() - 1; i >= 0; i--) {
            for (int j = (int)en.size() - 1; j >= 0; j--) {
                float dx2 = pb[i].x - en[j].x, dy2 = pb[i].y - en[j].y;
                if (dx2 * dx2 + dy2 * dy2 < (pb[i].r + en[j].r) * (pb[i].r + en[j].r)) {
                    en[j].hp -= pb[i].dmg;
                    addParticles(pb[i].x, pb[i].y, C_UI, 4, 120);
                    pb.erase(pb.begin() + i);
                    if (en[j].hp <= 0) killEnemy(j, false, en[j].type == EV_PROXY);
                    goto nextBullet;
                }
            }
            nextBullet:;
        }

        for (int i = (int)pb.size() - 1; i >= 0; i--) {
            for (int j = (int)eb.size() - 1; j >= 0; j--) {
                float dx2 = pb[i].x - eb[j].x, dy2 = pb[i].y - eb[j].y;
                if (dx2 * dx2 + dy2 * dy2 < (pb[i].r + eb[j].r) * (pb[i].r + eb[j].r)) {
                    addParticles(pb[i].x, pb[i].y, C_UI, 6, 100);
                    pb.erase(pb.begin() + i);
                    eb.erase(eb.begin() + j);
                    goto nextB;
                }
            }
            nextB:;
        }

        for (int i = (int)eb.size() - 1; i >= 0; i--) {
            float dx2 = eb[i].x - px, dy2 = eb[i].y - py;
            if (dx2 * dx2 + dy2 * dy2 < (eb[i].r + 12) * (eb[i].r + 12)) {
                damagePlayer(eb[i].dmg);
                eb.erase(eb.begin() + i);
            }
        }

        if (waveState == 0) {
            waveTimer -= dt;
            if (waveTimer <= 0) { startWave(); waveState = 1; spawnTimer = 0.5f; }
        } else if (waveState == 1) {
            spawnTimer -= dt;
            if (spawned < budget && spawnTimer <= 0) {
                spawnEnemy(pickEnemyType(wave));
                spawned++;
                float iv = max(0.25f, 1.4f - wave * 0.07f);
                spawnTimer = iv;
            }
            if (spawned >= budget) waveState = 2;
        } else {
            if (en.empty() && eb.empty()) {
                waveState = 0;
                waveTimer = 2.5f;
            }
        }

        for (int i = (int)part.size() - 1; i >= 0; i--) {
            part[i].x += part[i].vx * dt;
            part[i].y += part[i].vy * dt;
            part[i].vx *= 0.94f; part[i].vy *= 0.94f;
            part[i].life -= dt;
            if (part[i].life <= 0) part.erase(part.begin() + i);
        }
        for (int i = (int)ftext.size() - 1; i >= 0; i--) {
            ftext[i].y -= 40 * dt;
            ftext[i].life -= dt;
            if (ftext[i].life <= 0) ftext.erase(ftext.begin() + i);
        }
        if (shake > 0) shake = max(0.0f, shake - 40 * dt);
        if (flash > 0) flash = max(0.0f, flash - 2.0f * dt);
    }
    memset(kp, 0, sizeof(kp));
    mClick = false;
}

void Game::render() {
    if (!bufDC) {
        HDC sdc = GetDC(NULL);
        bufDC = CreateCompatibleDC(sdc);
        bufBmp = CreateCompatibleBitmap(sdc, LG_W, LG_H);
        ReleaseDC(NULL, sdc);
        SelectObject(bufDC, bufBmp);
    }
    HBRUSH br = CreateSolidBrush(C_BG);
    RECT r = { 0, 0, LG_W, LG_H };
    FillRect(bufDC, &r, br);
    DeleteObject(br);

    SetViewportOrgEx(bufDC, (int)((g_rng() % 1000 / 500.0f - 1) * shake), (int)((g_rng() % 1000 / 500.0f - 1) * shake), 0);

    for (size_t i = 0; i < bg.size(); i++) {
        HGDIOBJ op = CreatePen(PS_SOLID, 1, bg[i].c);
        HGDIOBJ oldp = SelectObject(bufDC, op);
        MoveToEx(bufDC, (int)bg[i].x1, (int)bg[i].y1, 0);
        LineTo(bufDC, (int)bg[i].x2, (int)bg[i].y2);
        SelectObject(bufDC, oldp);
        DeleteObject(op);
    }

    if (state == 4 || state == 5 || state == 6 || state == 7) {
        for (size_t i = 0; i < orbs.size(); i++) {
            Orb& o = orbs[i];
            if (o.kind == 0) drawCircle(bufDC, o.x, o.y, o.r, C_ORB, true);
            else {
                COLORREF c = o.kind == 1 ? RGB(255, 120, 120) : (o.kind == 2 ? C_ACCENT : C_ORB);
                drawCircle(bufDC, o.x, o.y, o.r + 2, c, false);
            }
        }
        for (size_t i = 0; i < eb.size(); i++) {
            float pulse = 1 + 0.15f * sinf((float)(i * 7) + elapsed * 6);
            drawCircle(bufDC, eb[i].x, eb[i].y, eb[i].r * pulse, RGB(255, 180, 60), true);
        }
        for (size_t i = 0; i < pb.size(); i++) {
            drawCircle(bufDC, pb[i].x, pb[i].y, pb[i].r, C_PLAYER, true);
        }
        for (size_t i = 0; i < en.size(); i++) drawShape(bufDC, en[i]);
        drawPlayer(bufDC);
        for (size_t i = 0; i < part.size(); i++) {
            Particle& p = part[i];
            drawCircle(bufDC, p.x, p.y, p.r, mixC(p.c, C_BG, 1 - p.life / p.maxlife), true);
        }
        for (size_t i = 0; i < ftext.size(); i++) {
            FText& t = ftext[i];
            text(bufDC, t.x - 60, t.y - 12, 120, 24, t.s.c_str(), t.c, fSmall, true, true);
        }
    }

    if (state == 0) drawMenu(bufDC);
    else if (state == 1) drawHowto(bufDC);
    else if (state == 2) drawScores(bufDC);
    else if (state == 3) drawSettings(bufDC);
    else if (state == 4) drawHUD(bufDC);
    else if (state == 5) drawLevelUp(bufDC);
    else if (state == 6) drawPause(bufDC);
    else if (state == 7) drawGameOver(bufDC);

    SetViewportOrgEx(bufDC, 0, 0, 0);

    int cw2, ch2;
    RECT cr;
    GetClientRect(hwnd, &cr);
    cw2 = cr.right - cr.left; ch2 = cr.bottom - cr.top;
    float sc = min((float)cw2 / LG_W, (float)ch2 / LG_H);
    int ww = (int)(LG_W * sc), wh = (int)(LG_H * sc);
    int ox = (cw2 - ww) / 2, oy = (ch2 - wh) / 2;
    HDC wdc = GetDC(hwnd);
    HBRUSH b2 = CreateSolidBrush(RGB(0, 0, 0));
    FillRect(wdc, &cr, b2);
    DeleteObject(b2);
    SetStretchBltMode(wdc, HALFTONE);
    StretchBlt(wdc, ox, oy, ww, wh, bufDC, 0, 0, LG_W, LG_H, SRCCOPY);
    ReleaseDC(hwnd, wdc);
}

void Game::drawCircle(HDC h, float x, float y, float rad, COLORREF c, bool fill) {
    HBRUSH br = fill ? CreateSolidBrush(c) : (HBRUSH)GetStockObject(NULL_BRUSH);
    HGDIOBJ oldb = SelectObject(h, br);
    HPEN pen = fill ? CreatePen(PS_SOLID, 1, c) : CreatePen(PS_SOLID, 2, c);
    HGDIOBJ oldp = SelectObject(h, pen);
    Ellipse(h, (int)(x - rad), (int)(y - rad), (int)(x + rad), (int)(y + rad));
    SelectObject(h, oldb);
    SelectObject(h, oldp);
    DeleteObject(br);
    DeleteObject(pen);
}

void Game::buildSprites() {
    static const int fs[7] = { 56, 40, 32, 48, 64, 40, 120 };
    for (int t = 0; t < 7; t++) {
        Sprite& s = sp[t];
        s.fw = s.fh = fs[t];
        s.sw = s.sh = 256;
        s.srcBmp = makeDib(256, 256, SPRKEY, &s.srcPix);
        HDC sdc = CreateCompatibleDC(NULL);
        SelectObject(sdc, s.srcBmp);
        artSprite(sdc, t, (int)(256.0f / fs[t] + 0.5f));
        DeleteDC(sdc);
        for (int k = 0; k < SPR_N; k++) {
            DWORD* fp = 0;
            HBITMAP fb = makeDib(fs[t], fs[t], SPRKEY, &fp);
            rotateFrame(s.srcPix, 256, 256, fp, fs[t], fs[t], -k * 2 * PI / SPR_N);
            s.frames.push_back(fb);
            HDC dc = CreateCompatibleDC(NULL);
            SelectObject(dc, fb);
            s.dcs.push_back(dc);
        }
    }
}

int Game::frameIndex(float ang) {
    while (ang < 0) ang += (float)(2 * PI);
    while (ang >= 2 * PI) ang -= (float)(2 * PI);
    int i = (int)(ang / (float)(2 * PI / SPR_N));
    if (i >= SPR_N) i = SPR_N - 1;
    return i;
}

void Game::drawSprite(HDC h, int type, float x, float y, float ang, float scale) {
    Sprite& s = sp[type];
    int fi = frameIndex(ang);
    int w = (int)(s.fw * scale);
    int hh = (int)(s.fh * scale);
    TransparentBlt(h, (int)(x - w / 2), (int)(y - hh / 2), w, hh,
        s.dcs[fi], 0, 0, s.fw, s.fh, SPRKEY);
}

void Game::drawShape(HDC h, const Enemy& e) {
    COLORREF c;
    int type;
    switch (e.type) {
        case EV_VIRUS: c = C_VIRUS; type = 1; break;
        case EV_BOTNET: c = C_BOT; type = 2; break;
        case EV_SPAMMER: c = C_SPAM; type = 3; break;
        case EV_TANK: c = C_TANK; type = 4; break;
        case EV_PROXY: c = C_PROXY; type = 5; break;
        default: c = C_BOSS; type = 6; break;
    }
    float pulse = 1 + 0.08f * sinf(e.born > 0 ? (0.5f - e.born) * 30 : elapsed * 5 + e.x * 0.05f);
    drawCircle(h, e.x, e.y, e.r * pulse * 1.8f, mixC(c, C_BG, 0.72f), true);
    drawSprite(h, type, e.x, e.y, atan2f(py - e.y, px - e.x), pulse);
}

void Game::drawPlayer(HDC h) {
    if (invuln > 0 && ((int)(elapsed * 20) % 2 == 0)) return;
    drawCircle(h, px, py, 24, mixC(C_PLAYER, C_BG, 0.76f), true);
    drawSprite(h, 0, px, py, pa, 1 + 0.04f * sinf(elapsed * 4));
}

void Game::text(HDC h, float x, float y, float w, float hh, const wchar_t* s, COLORREF c, HFONT f, bool ch, bool cv) {
    SetTextColor(h, c);
    SetBkMode(h, TRANSPARENT);
    HGDIOBJ old = SelectObject(h, f);
    RECT r = { (int)x, (int)y, (int)(x + w), (int)(y + hh) };
    DWORD fmt = DT_NOCLIP | DT_SINGLELINE;
    if (ch) fmt |= DT_CENTER;
    if (cv) fmt |= DT_VCENTER;
    DrawTextW(h, s, -1, &r, fmt);
    SelectObject(h, old);
}

void Game::drawBar(HDC h, float x, float y, float w, float hh, float frac, COLORREF c) {
    frac = max(0.0f, min(1.0f, frac));
    HBRUSH bb = CreateSolidBrush(RGB(30, 34, 46));
    HGDIOBJ oldb = SelectObject(h, bb);
    Rectangle(h, (int)x, (int)y, (int)(x + w), (int)(y + hh));
    SelectObject(h, oldb);
    DeleteObject(bb);
    if (frac > 0) {
        HBRUSH bc = CreateSolidBrush(c);
        oldb = SelectObject(h, bc);
        Rectangle(h, (int)x, (int)y, (int)(x + w * frac), (int)(y + hh));
        SelectObject(h, oldb);
        DeleteObject(bc);
    }
}

void Game::drawHUD(HDC h) {
    drawBar(h, 20, 16, 340, 14, hp / maxhp, C_RED);
    drawBar(h, 20, 34, 340, 10, shield / 150.0f, C_ACCENT);
    float xpf = lvl >= MAX_LVL ? 1.0f : xp / xpNext;
    drawBar(h, 20, 48, 340, 8, xpf, C_ORB);
    wchar_t buf[128];
    swprintf(buf, 128, L"СЧЁТ %d", score);
    text(h, LG_W - 200, 8, 180, 26, buf, C_UI, fMid, false, true);
    swprintf(buf, 128, L"ВОЛНА %d", wave);
    text(h, LG_W - 200, 36, 180, 24, buf, C_ACCENT, fSmall, false, true);
    swprintf(buf, 128, L"УРОВЕНЬ %d", lvl);
    text(h, LG_W - 200, 62, 180, 20, buf, C_ORB, fTiny, false, true);

    int bx = LG_W - 190, by = LG_H - 40;
    float cd = dashCd / 2.0f;
    drawCircle(h, bx, by, 9, cd > 0 ? mixC(C_UI, C_BG, 0.7f) : C_PLAYER, true);
    text(h, bx - 16, by - 8, 32, 16, L"ПРОБЕЛ", C_UI, fTiny, true, true);
    bx += 46;
    cd = abilCd / 15.0f;
    drawCircle(h, bx, by, 9, cd > 0 ? mixC(C_UI, C_BG, 0.7f) : C_BOSS, true);
    text(h, bx - 16, by - 8, 32, 16, L"E", C_UI, fTiny, true, true);
    bx += 46;
    cd = slowCd / 20.0f;
    drawCircle(h, bx, by, 9, cd > 0 ? mixC(C_UI, C_BG, 0.7f) : C_SPAM, true);
    text(h, bx - 16, by - 8, 32, 16, L"Q", C_UI, fTiny, true, true);

    const wchar_t* names[3] = { L"1 АПТЕЧКА", L"2 ЩИТ", L"3 БОМБА" };
    for (int i = 0; i < 3; i++) {
        int wx = 30 + i * 130;
        COLORREF c = consum[i] > 0 ? C_UI : darken(C_UI, 0.7f);
        swprintf(buf, 128, L"%ls x%d", names[i], consum[i]);
        text(h, wx, LG_H - 34, 120, 22, buf, c, fTiny, false, true);
    }
    if (slowTimer > 0) {
        text(h, LG_W / 2 - 150, LG_H / 2 - 120, 300, 40, L"ЗАМЕДЛЕНИЕ", C_SPAM, fMid, true, true);
    }
    if (waveState == 0 && waveTimer > 0) {
        swprintf(buf, 128, wave % 5 == 0 ? L"ВНИМАНИЕ: БОСС!" : L"ВОЛНА %d", wave + 1);
        text(h, LG_W / 2 - 200, LG_H / 2 - 100, 400, 60, buf, wave % 5 == 0 ? C_BOSS : C_ACCENT, fBig, true, true);
    }
}

void Game::drawMenu(HDC h) {
    text(h, 0, 90, LG_W, 80, L"СИГНАЛ", C_PLAYER, fTitle, true, true);
    text(h, 0, 175, LG_W, 30, L"выживи в повреждённой сети", C_UI, fSmall, true, true);
    int y = 260;
    for (int i = 0; i < 5; i++) {
        int yy = y + i * 52;
        bool sel = i == menuSel;
        if (sel) {
            HBRUSH br = CreateSolidBrush(mixC(C_ACCENT, C_BG, 0.85f));
            HGDIOBJ oldb = SelectObject(h, br);
            RoundRect(h, LG_W / 2 - 170, yy - 14, LG_W / 2 + 170, yy + 26, 12, 12);
            SelectObject(h, oldb);
            DeleteObject(br);
        }
        text(h, 0, yy - 14, LG_W, 40, MENU_ITEMS[i], sel ? C_PLAYER : C_UI, sel ? fBig : fMid, true, true);
    }
    text(h, 0, LG_H - 40, LG_W, 24, L"W/S — выбор   Enter — подтвердить   F — экран   M — звук", C_UI, fTiny, true, true);
}

void Game::drawHowto(HDC h) {
    text(h, 0, 20, LG_W, 50, L"КАК ИГРАТЬ", C_ACCENT, fBig, true, true);
    static const wchar_t* P1[] = {
        L"УПРАВЛЕНИЕ",
        L"W/A/S/D или стрелки — движение",
        L"Мышь — прицеливание",
        L"ЛКМ (удерживать) — стрельба",
        L"Пробел — рывок (2 сек)",
        L"E — ПЕРЕЗАГРУЗКА: убивает всех врагов (15 сек)",
        L"Q — замедление времени (20 сек)",
        L"1/2/3 — аптечка / щит / бомба",
        L"P или Esc — пауза",
        L"F — полноэкранный режим,  M — звук"
    };
    static const wchar_t* P2[] = {
        L"ВРАГИ",
        L"ВИРУС — красный, летит прямо на тебя",
        L"БОТНЕТ — оранжевый, быстрый и мелкий",
        L"СПАМЕР — зелёный, стреляет снарядами",
        L"ТАНК — фиолетовый, толстый и медленный",
        L"ПРОКСИ — серый, взрывается рядом",
        L"БОСС КРАКЕН — каждые 5 волн, веер снарядов",
        L"Сбивай вражеские снаряды своими пулями!"
    };
    static const wchar_t* P3[] = {
        L"ПРОКАЧКА",
        L"Собирай дата-ядра (+1 опыт)",
        L"Каждый уровень — выбор 1 из 3 улучшений",
        L"Улучшения: урон, скорострельность, щит,",
        L"веер, двойной выстрел, вампиризм и другие",
        L"Опыт выпадает из всех врагов",
        L"Босс даёт сразу 15 ядер"
    };
    static const wchar_t* P4[] = {
        L"СОВЕТЫ",
        L"1. Двигайся постоянно — стоять нельзя",
        L"2. Рывок даёт неуязвимость — спасает от босса",
        L"3. Бомбу (3) копи против толпы",
        L"4. ПЕРЕЗАГРУЗКА (E) — спасение в окружении",
        L"5. Взрывай ПРОКСИ на расстоянии",
        L"6. Сложность СЛОЖНАЯ даёт x1.5 к очкам",
        L"7. Аптечки падают с врагов — подбирай"
    };
    static const wchar_t* const* pages[4] = { P1, P2, P3, P4 };
    static const int counts[4] = { 10, 7, 7, 7 };
    int y = 90;
    for (int i = 0; i < counts[howPage]; i++) {
        text(h, 60, y, LG_W - 120, 34, pages[howPage][i], i == 0 ? C_ACCENT : C_UI, i == 0 ? fMid : fSmall, false, true);
        y += i == 0 ? 26 : 36;
    }
    wchar_t buf[128];
    swprintf(buf, 128, L"страница %d/4   A/D — листать   Esc — назад", howPage + 1);
    text(h, 0, LG_H - 50, LG_W, 26, buf, C_UI, fTiny, true, true);
}

void Game::drawScores(HDC h) {
    text(h, 0, 20, LG_W, 50, L"РЕКОРДЫ", C_ACCENT, fBig, true, true);
    if (rec.empty()) {
        text(h, 0, 260, LG_W, 40, L"Пока нет рекордов — сыграй!", C_UI, fMid, true, true);
    }
    for (int i = 0; i < (int)rec.size() && i < MAX_RECORDS; i++) {
        int yy = 110 + i * 42;
        wchar_t buf[160];
        swprintf(buf, 160, L"%d.", i + 1);
        text(h, 80, yy, 120, 30, buf, i == 0 ? C_ORB : C_UI, fSmall, false, true);
        swprintf(buf, 160, L"очки: %d", rec[i].score);
        text(h, 220, yy, 220, 30, buf, C_PLAYER, fSmall, false, true);
        swprintf(buf, 160, L"волна %d", rec[i].wave);
        text(h, 470, yy, 160, 30, buf, C_UI, fSmall, false, true);
        wchar_t wdate[64];
        ansiToWide(rec[i].date, wdate, 64);
        text(h, 650, yy, 220, 30, wdate, C_UI, fTiny, false, true);
    }
    text(h, 0, LG_H - 50, LG_W, 26, L"X — очистить    Esc — назад", C_UI, fTiny, true, true);
}

void Game::drawSettings(HDC h) {
    text(h, 0, 20, LG_W, 50, L"НАСТРОЙКИ", C_ACCENT, fBig, true, true);
    const wchar_t* diffs[3] = { L"ЛЁГКАЯ", L"ОБЫЧНАЯ", L"СЛОЖНАЯ" };
    wchar_t buf[160];
    const wchar_t* items[5] = { L"Сложность", L"Громкость", L"Звук", L"Полный экран", L"Авто-прицел" };
    wchar_t vals[5][32];
    swprintf(vals[0], 32, L"%ls", diffs[difficulty]);
    swprintf(vals[1], 32, L"%d%%", volume);
    swprintf(vals[2], 32, L"%ls", soundOn ? L"ВКЛ" : L"ВЫКЛ");
    swprintf(vals[3], 32, L"%ls", fullscreen ? L"ВКЛ" : L"ВЫКЛ");
    swprintf(vals[4], 32, L"%ls", autoaim ? L"ВКЛ" : L"ВЫКЛ");
    for (int i = 0; i < 5; i++) {
        int yy = 110 + i * 56;
        bool sel = i == settingsSel;
        if (sel) {
            HBRUSH br = CreateSolidBrush(mixC(C_ACCENT, C_BG, 0.88f));
            HGDIOBJ oldb = SelectObject(h, br);
            RoundRect(h, LG_W / 2 - 260, yy - 12, LG_W / 2 + 260, yy + 32, 12, 12);
            SelectObject(h, oldb);
            DeleteObject(br);
        }
        text(h, LG_W / 2 - 240, yy - 10, 260, 28, items[i], sel ? C_PLAYER : C_UI, fMid, false, true);
        text(h, LG_W / 2 + 20, yy - 10, 220, 28, vals[i], sel ? C_ORB : C_UI, fMid, false, true);
    }
    text(h, 0, LG_H - 60, LG_W, 26, L"A/D или ←/→ — изменить   Esc — назад", C_UI, fTiny, true, true);
    swprintf(buf, 160, L"Сложная даёт x1.5 к очкам. Сейчас рекорд: %d", rec.empty() ? 0 : rec[0].score);
    text(h, 0, LG_H - 34, LG_W, 24, buf, C_UI, fTiny, true, true);
}

void Game::drawLevelUp(HDC h) {
    HBRUSH ov = CreateSolidBrush(RGB(0, 0, 0));
    HGDIOBJ oldb = SelectObject(h, ov);
    Rectangle(h, 0, 0, LG_W, LG_H);
    SelectObject(h, oldb);
    DeleteObject(ov);
    wchar_t buf[64];
    swprintf(buf, 64, L"УРОВЕНЬ %d! ВЫБЕРИ УЛУЧШЕНИЕ", lvl);
    text(h, 0, 70, LG_W, 50, buf, C_ORB, fBig, true, true);
    int cardW = 260, gap = 24, totalW = cardW * 3 + gap * 2;
    int x0 = (LG_W - totalW) / 2, cy = 240;
    for (int i = 0; i < 3; i++) {
        int cx = x0 + i * (cardW + gap);
        RECT cr = { cx, cy, cx + cardW, cy + 220 };
        cardR[i] = cr;
        HBRUSH bb = CreateSolidBrush(mixC(C_BG, C_ACCENT, 0.08f));
        HGDIOBJ ob = SelectObject(h, bb);
        RoundRect(h, cr.left, cr.top, cr.right, cr.bottom, 14, 14);
        SelectObject(h, ob);
        DeleteObject(bb);
        HPEN pen = CreatePen(PS_SOLID, 2, choices[i] >= 0 ? C_ACCENT : darken(C_UI, 0.6f));
        HGDIOBJ op = SelectObject(h, pen);
        HGDIOBJ obr = SelectObject(h, (HBRUSH)GetStockObject(NULL_BRUSH));
        RoundRect(h, cr.left, cr.top, cr.right, cr.bottom, 14, 14);
        SelectObject(h, op);
        SelectObject(h, obr);
        DeleteObject(pen);
        if (choices[i] < 0) {
            text(h, cx, cy + 80, cardW, 40, L"—", C_UI, fMid, true, true);
            continue;
        }
        wchar_t key[8];
        swprintf(key, 8, L"[%d]", i + 1);
        text(h, cx, cy + 14, cardW, 28, key, C_ACCENT, fSmall, true, true);
        text(h, cx + 10, cy + 50, cardW - 20, 40, UPG_NAME[choices[i]], C_PLAYER, fMid, true, true);
        text(h, cx + 10, cy + 100, cardW - 20, 80, UPG_DESC[choices[i]], C_UI, fSmall, true, true);
        swprintf(buf, 64, L"уровень %d/%d", up[choices[i]] + 1, UPG_MAX[choices[i]]);
        text(h, cx, cy + 180, cardW, 24, buf, C_UI, fTiny, true, true);
    }
    text(h, 0, LG_H - 60, LG_W, 26, L"1/2/3 или клик по карточке — выбрать    Esc — пропустить", C_UI, fTiny, true, true);
}

void Game::drawPause(HDC h) {
    HBRUSH ov = CreateSolidBrush(RGB(0, 0, 0));
    HGDIOBJ oldb = SelectObject(h, ov);
    Rectangle(h, 0, 0, LG_W, LG_H);
    SelectObject(h, oldb);
    DeleteObject(ov);
    text(h, 0, 120, LG_W, 60, L"ПАУЗА", C_ACCENT, fBig, true, true);
    for (int i = 0; i < 4; i++) {
        int yy = 250 + i * 56;
        bool sel = i == pauseSel;
        if (sel) {
            HBRUSH br = CreateSolidBrush(mixC(C_ACCENT, C_BG, 0.85f));
            HGDIOBJ ob = SelectObject(h, br);
            RoundRect(h, LG_W / 2 - 160, yy - 14, LG_W / 2 + 160, yy + 30, 12, 12);
            SelectObject(h, ob);
            DeleteObject(br);
        }
        text(h, 0, yy - 12, LG_W, 42, PAUSE_ITEMS[i], sel ? C_PLAYER : C_UI, sel ? fBig : fMid, true, true);
    }
}

void Game::drawGameOver(HDC h) {
    HBRUSH ov = CreateSolidBrush(mixC(C_BG, RGB(60, 0, 0), 0.4f));
    HGDIOBJ oldb = SelectObject(h, ov);
    Rectangle(h, 0, 0, LG_W, LG_H);
    SelectObject(h, oldb);
    DeleteObject(ov);
    text(h, 0, 90, LG_W, 70, L"СИГНАЛ ПОТЕРЯН", C_RED, fTitle, true, true);
    if (newRec) text(h, 0, 165, LG_W, 36, L"НОВЫЙ РЕКОРД!", C_ORB, fBig, true, true);
    wchar_t buf[160];
    int yy = 250;
    swprintf(buf, 160, L"СЧЁТ: %d", score);
    text(h, 0, yy, LG_W, 36, buf, C_UI, fBig, true, true); yy += 44;
    swprintf(buf, 160, L"Волна: %d    Убийств: %d    Время: %d сек", wave, kills, (int)elapsed);
    text(h, 0, yy, LG_W, 30, buf, C_UI, fSmall, true, true); yy += 40;
    swprintf(buf, 160, L"Улучшений получено: %d", up[0] + up[1] + up[2] + up[3] + up[4] + up[5] + up[6] + up[7] + up[8] + up[9] + up[10] + up[11]);
    text(h, 0, yy, LG_W, 30, buf, C_UI, fSmall, true, true);
    text(h, 0, LG_H - 70, LG_W, 28, L"R — заново    Enter — меню    Esc — выход", C_UI, fTiny, true, true);
}

void Game::applyFullscreen(bool fs) {
    if (fs == fullscreen) return;
    fullscreen = fs;
    if (fs) {
        GetWindowRect(hwnd, &winRect);
        LONG style = GetWindowLongPtrW(hwnd, GWL_STYLE);
        style &= ~WS_OVERLAPPEDWINDOW;
        style |= WS_POPUP;
        SetWindowLongPtrW(hwnd, GWL_STYLE, style);
        int w = GetSystemMetrics(SM_CXSCREEN), h = GetSystemMetrics(SM_CYSCREEN);
        SetWindowPos(hwnd, HWND_TOP, 0, 0, w, h, SWP_FRAMECHANGED);
    } else {
        LONG style = GetWindowLongPtrW(hwnd, GWL_STYLE);
        style &= ~WS_POPUP;
        style |= WS_OVERLAPPEDWINDOW;
        SetWindowLongPtrW(hwnd, GWL_STYLE, style);
        SetWindowPos(hwnd, HWND_TOP, winRect.left, winRect.top,
            winRect.right - winRect.left, winRect.bottom - winRect.top, SWP_FRAMECHANGED);
    }
    saveSettings();
}

static void ansiToWide(const char* src, wchar_t* dst, int dstLen) {
    int n = MultiByteToWideChar(CP_ACP, 0, src, -1, dst, dstLen);
    if (n <= 0) dst[0] = 0;
}

FILE* openCfg(const wchar_t* name, const wchar_t* mode) {
    wchar_t p[MAX_PATH * 2];
    swprintf(p, MAX_PATH * 2, L"%ls\\%ls", g_exeDir, name);
    return _wfopen(p, mode);
}

void Game::loadSettings() {
    difficulty = 1; volume = 70; soundOn = true; autoaim = false;
    FILE* f = openCfg(L"signal_settings.dat", L"rt");
    if (f) {
        char line[256];
        while (fgets(line, sizeof(line), f)) {
            int v;
            if (sscanf(line, "difficulty=%d", &v) == 1) difficulty = v;
            if (sscanf(line, "volume=%d", &v) == 1) volume = v;
            if (sscanf(line, "sound=%d", &v) == 1) soundOn = v != 0;
            if (sscanf(line, "autoaim=%d", &v) == 1) autoaim = v != 0;
        }
        fclose(f);
    }
}

void Game::saveSettings() {
    FILE* f = openCfg(L"signal_settings.dat", L"wt");
    if (f) {
        fprintf(f, "difficulty=%d\nvolume=%d\nsound=%d\nfullscreen=%d\nautoaim=%d\n",
            difficulty, volume, soundOn ? 1 : 0, fullscreen ? 1 : 0, autoaim ? 1 : 0);
        fclose(f);
    }
}

void Game::loadRecords() {
    rec.clear();
    FILE* f = openCfg(L"signal_scores.dat", L"rt");
    if (f) {
        while (rec.size() < MAX_RECORDS) {
            Record r;
            char date[64] = "";
            if (fscanf(f, "%d %d %63[^\n]", &r.score, &r.wave, date) == 3) {
                size_t dl = strlen(date);
                while (dl > 0 && (date[dl - 1] == '\r' || date[dl - 1] == ' ' || date[dl - 1] == '\n')) date[--dl] = 0;
                memcpy(r.date, date, 32);
                r.date[31] = 0;
                rec.push_back(r);
            } else break;
        }
        fclose(f);
    }
    sort(rec.begin(), rec.end(), [](const Record& a, const Record& b) { return a.score > b.score; });
}

void Game::saveRecords() {
    FILE* f = openCfg(L"signal_scores.dat", L"wt");
    if (f) {
        for (size_t i = 0; i < rec.size() && i < MAX_RECORDS; i++) {
            fprintf(f, "%d %d %s\n", rec[i].score, rec[i].wave, rec[i].date);
        }
        fclose(f);
    }
}

void Game::recordScore() {
    newRec = !rec.empty() && score > rec[0].score;
    SYSTEMTIME st;
    GetLocalTime(&st);
    Record r;
    r.score = score; r.wave = wave;
    sprintf(r.date, "%04d-%02d-%02d %02d:%02d", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute);
    rec.push_back(r);
    sort(rec.begin(), rec.end(), [](const Record& a, const Record& b) { return a.score > b.score; });
    if (rec.size() > MAX_RECORDS) rec.resize(MAX_RECORDS);
    saveRecords();
}

static void writeWav(const wchar_t* path, const vector<short>& data) {
    FILE* f = _wfopen(path, L"wb");
    if (!f) return;
    int sr = 22050;
    unsigned size = 36 + (unsigned)data.size() * 2;
    fwrite("RIFF", 1, 4, f);
    fwrite(&size, 4, 1, f);
    fwrite("WAVE", 1, 4, f);
    fwrite("fmt ", 1, 4, f);
    unsigned sub = 16;
    fwrite(&sub, 4, 1, f);
    short fmt = 1, ch = 1, bits = 16;
    fwrite(&fmt, 2, 1, f);
    fwrite(&ch, 2, 1, f);
    fwrite(&sr, 4, 1, f);
    unsigned br2 = sr * 2, ba = 2;
    fwrite(&br2, 4, 1, f);
    fwrite(&ba, 2, 1, f);
    fwrite(&bits, 2, 1, f);
    fwrite("data", 1, 4, f);
    unsigned dsz = (unsigned)data.size() * 2;
    fwrite(&dsz, 4, 1, f);
    fwrite(data.data(), 2, data.size(), f);
    fclose(f);
}

static void genTone(const wchar_t* name, double f0, double f1, double dur, float vol, int wave) {
    int sr = 22050, n = (int)(sr * dur);
    vector<short> buf(n);
    double ph = 0;
    for (int i = 0; i < n; i++) {
        double t = (double)i / sr;
        double prog = t / dur;
        double f = f0 + (f1 - f0) * prog;
        ph += 2 * PI * f / sr;
        double env = 1 - prog;
        if (prog < 0.02) env = prog / 0.02;
        double v = 0;
        if (wave == 0) v = sin(ph);
        else if (wave == 1) v = sin(ph) > 0 ? 0.6 : -0.6;
        else v = ((g_rng() % 1000) / 500.0 - 1) * 0.6;
        buf[i] = (short)(v * env * 30000 * vol);
    }
    wchar_t p[MAX_PATH * 2];
    swprintf(p, MAX_PATH * 2, L"%ls\\snd_%ls.wav", g_soundDir, name);
    writeWav(p, buf);
}

void Game::genAllSounds() {
    float vol = volume / 100.0f;
    genTone(L"shot", 880, 440, 0.06f, vol * 0.35f, 1);
    genTone(L"hit", 300, 150, 0.08f, vol * 0.6f, 1);
    genTone(L"explode", 220, 50, 0.28f, vol * 0.7f, 2);
    genTone(L"hurt", 400, 100, 0.2f, vol * 0.7f, 2);
    genTone(L"die", 320, 40, 0.9f, vol * 0.8f, 2);
    genTone(L"pickup", 700, 1000, 0.1f, vol * 0.5f, 0);
    genTone(L"ability", 1500, 200, 0.3f, vol * 0.7f, 1);
    genTone(L"dash", 500, 800, 0.12f, vol * 0.4f, 0);
    vector<short> arp;
    double fs[4] = { 523.25, 659.25, 783.99, 1046.5 };
    for (int k = 0; k < 4; k++) {
        int sr = 22050, n = (int)(sr * 0.11);
        for (int i = 0; i < n; i++) {
            double t = (double)i / sr;
            double env = 1 - t / 0.11;
            double v = sin(2 * PI * fs[k] * t) * env * 0.6;
            arp.push_back((short)(v * 30000 * vol));
        }
    }
    wchar_t p[MAX_PATH * 2];
    swprintf(p, MAX_PATH * 2, L"%ls\\snd_levelup.wav", g_soundDir);
    writeWav(p, arp);
}

void Game::playSnd(const wchar_t* name) {
    if (!soundOn || volume <= 0) return;
    wchar_t p[MAX_PATH * 2];
    swprintf(p, MAX_PATH * 2, L"%ls\\snd_%ls.wav", g_soundDir, name);
    PlaySoundW(p, NULL, SND_FILENAME | SND_ASYNC | SND_NODEFAULT);
}

static void createFonts(Game& g) {
    g.fTiny = CreateFontW(-15, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, FIXED_PITCH, L"Consolas");
    g.fSmall = CreateFontW(-18, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, FIXED_PITCH, L"Consolas");
    g.fMid = CreateFontW(-24, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, FIXED_PITCH, L"Consolas");
    g.fBig = CreateFontW(-34, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, FIXED_PITCH, L"Consolas");
    g.fTitle = CreateFontW(-58, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, FIXED_PITCH, L"Consolas");
}

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_CREATE: {
            g_rng.seed((unsigned)time(0) ^ GetTickCount());
            GetTempPathW(MAX_PATH, g_soundDir);
            size_t l = wcslen(g_soundDir);
            if (l > 0 && g_soundDir[l - 1] == L'\\') g_soundDir[l - 1] = 0;
            GetModuleFileNameW(NULL, g_exeDir, MAX_PATH);
            for (int i = (int)wcslen(g_exeDir) - 1; i >= 0; i--) {
                if (g_exeDir[i] == L'\\') { g_exeDir[i] = 0; break; }
            }
            G.init(hwnd);
            createFonts(G);
            return 0;
        }
        case WM_PAINT: {
            PAINTSTRUCT ps;
            BeginPaint(hwnd, &ps);
            G.render();
            EndPaint(hwnd, &ps);
            return 0;
        }
        case WM_ERASEBKGND:
            return 1;
        case WM_KEYDOWN:
            if (!(lParam & (1 << 30))) G.kp[wParam & 0xFF] = true;
            G.kb[wParam & 0xFF] = true;
            return 0;
        case WM_KEYUP:
            G.kb[wParam & 0xFF] = false;
            return 0;
        case WM_LBUTTONDOWN:
            G.mDown = true;
            return 0;
        case WM_LBUTTONUP:
            G.mDown = false;
            G.mClick = true;
            return 0;
        case WM_MOUSEMOVE: {
            RECT cr;
            GetClientRect(hwnd, &cr);
            int cw2 = cr.right - cr.left, ch2 = cr.bottom - cr.top;
            float sc = min((float)cw2 / LG_W, (float)ch2 / LG_H);
            int ww = (int)(LG_W * sc), wh = (int)(LG_H * sc);
            int ox = (cw2 - ww) / 2, oy = (ch2 - wh) / 2;
            G.mx = (int)(((short)LOWORD(lParam) - ox) / sc);
            G.my = (int)(((short)HIWORD(lParam) - oy) / sc);
            return 0;
        }
        case WM_DESTROY:
            G.saveSettings();
            G.saveRecords();
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

int WINAPI WinMain(HINSTANCE hInst, HINSTANCE, LPSTR, int nCmdShow) {
    timeBeginPeriod(1);
    WNDCLASSW wc = {};
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInst;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = NULL;
    wc.lpszClassName = L"SignalGame";
    if (!RegisterClassW(&wc)) return 0;

    RECT wr = { 0, 0, LG_W, LG_H };
    AdjustWindowRect(&wr, WS_OVERLAPPEDWINDOW, FALSE);
    HWND hwnd = CreateWindowW(L"SignalGame", L"СИГНАЛ — арена-выживание",
        WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
        wr.right - wr.left, wr.bottom - wr.top, NULL, NULL, hInst, NULL);
    if (!hwnd) return 0;
    ShowWindow(hwnd, nCmdShow);
    UpdateWindow(hwnd);

    MSG msg;
    DWORD acc = 0;
    DWORD prev = timeGetTime();
    while (true) {
        while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) { timeEndPeriod(1); return (int)msg.wParam; }
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        DWORD now = timeGetTime();
        DWORD elapsed = now - prev;
        prev = now;
        if (elapsed > 50) elapsed = 50;
        acc += elapsed;
        if (acc >= 16) {
            acc = 0;
            G.update(16.0f / 1000.0f);
            G.render();
            memset(G.kp, 0, sizeof(G.kp));
            G.mClick = false;
        } else {
            Sleep(1);
        }
    }
}
