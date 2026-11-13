// PanelView.mm
// The custom panel for TB Effectron: an original drawing laid out like the
// user's Effectron I (ADM 1020), in the unit's own colours as the user asked:
// the blue 1U face between dark rack ears, white lettering, black knurled
// knobs with teal caps (SPEED too, at the user's request; the concentric
// DELAY / WIDTH pair),
// the two red DIGITAL DELAY push buttons with their range legend, the LIMIT
// LED, the MODULATOR outline. The DeltaLab badge is left off; REPEAT (the
// rear footswitch's infinite repeat) sits in its place as a third red push
// button, and the user's TB electronic logo, red and black, is at the right-
// hand end. Every push button lights up when it is in. The face is one
// panel: the plugin controls (Input, Output, red preset buttons, readout)
// sit along its lower part. Square corners throughout.
//
// Cocoa, manual retain/release, and only APIs that exist in Mac OS X 10.8.5.
// Everything is drawn in code; no image files.

#import <Cocoa/Cocoa.h>

#include "Editor.h"
#include "Params.h"
#include "Presets.h"
#include "TBLogo.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

using namespace tbeffectron;

namespace
{

enum CtlType { kCtlKnob, kCtlOuterRing, kCtlInnerCap, kCtlRangeButton, kCtlPushButton, kCtlSmallKnob };

struct Ctl
{
    int param;
    int type;
    CGFloat cx, cy, r;     // r: knob radius (inner cap radius for WIDTH); buttons: half width
    int which;             // range buttons: 0 left, 1 right
    const char* label;     // readout name
    const char* posA;      // small knob left end
    const char* posB;      // small knob right end
};

// Geometry: the photo (699 x 78) scaled by 1.8; the strip below
const CGFloat kEarW = 22.0;                 // dark rack ears either side
const CGFloat kBlueTop = 14.0, kBlueMargin = 14.0;   // the blue face, top and bottom margins
const CGFloat kKnobR = 20.0;
const CGFloat kCapR = 14.5;
const CGFloat kButtonHalfW = 10.0, kButtonHalfH = 15.0;

const Ctl kCtls[] = {
    { kLevel,    kCtlKnob,         211,  75, kKnobR, 0, "LEVEL",    0, 0 },
    { kFeedback, kCtlKnob,         349,  75, kKnobR, 0, "FEEDBACK", 0, 0 },
    { kRange,    kCtlRangeButton,  506,  82, kButtonHalfW, 0, "DIGITAL DELAY", 0, 0 },
    { kRange,    kCtlRangeButton,  574,  82, kButtonHalfW, 1, "DIGITAL DELAY", 0, 0 },
    { kWidth,    kCtlInnerCap,     733,  75, kCapR,  0, "WIDTH",    0, 0 },
    { kDelay,    kCtlOuterRing,    733,  75, kKnobR, 0, "DELAY",    0, 0 },
    { kSpeed,    kCtlKnob,         895,  75, kKnobR, 0, "SPEED",    0, 0 },
    { kMix,      kCtlKnob,        1049,  75, kKnobR, 0, "MIX",      0, 0 },
    { kRepeat,   kCtlPushButton,    72,  72, kButtonHalfW, 0, "REPEAT", 0, 0 },
    // Plugin strip
    { kInput,    kCtlSmallKnob,     44, 190, 17, 0, "INPUT",  "-12", "+12" },
    { kOutput,   kCtlSmallKnob,    116, 190, 17, 0, "OUTPUT", "-24", "+24" },
};
const int kNumCtls = (int) (sizeof (kCtls) / sizeof (kCtls[0]));

const NSPoint kLedLimit = { 153.0, 92.0 };
const NSRect kLogoBox = { { 1108.0, 28.0 }, { 104.0, 58.0 } };

// Strip: preset section, then the readout
const CGFloat kStripLabelY = 158.0;
const NSRect kSaveRect = { { 168.0, 177.0 }, { 46.0, 26.0 } };
const CGFloat kSlotX0 = 222.0, kSlotPitch = 33.0, kSlotW = 29.0, kSlotY = 177.0, kSlotH = 26.0;
const NSRect kReadoutRect = { { 500.0, 167.0 }, { 744.0, 46.0 } };

inline NSRect slotRect (int k) { return NSMakeRect (kSlotX0 + k * kSlotPitch, kSlotY, kSlotW, kSlotH); }

// Pointer angle (radians, 0 = up, clockwise positive) for a 0..1 value over 300 degrees.
inline CGFloat valueAngle (CGFloat v) { return (CGFloat) ((-150.0 + 300.0 * v) * M_PI / 180.0); }
// WIDTH (inner cap): 12 o'clock (0 %) to 5 o'clock (100 %)
inline CGFloat widthAngle (CGFloat v) { return (CGFloat) ((150.0 * v) * M_PI / 180.0); }

// Point at angle a (0 = up, clockwise) and radius r around (cx, cy) in flipped coordinates.
inline NSPoint polar (CGFloat cx, CGFloat cy, CGFloat r, CGFloat a)
{
    return NSMakePoint (cx + r * std::sin (a), cy - r * std::cos (a));
}

NSColor* rgb (int r, int g, int b) { return [NSColor colorWithCalibratedRed: r / 255.0 green: g / 255.0 blue: b / 255.0 alpha: 1.0]; }
// The unit's colours (from the photo)
NSColor* ink()      { return rgb (228, 231, 238); }     // white lettering on the blue
NSColor* blueTop()  { return rgb (58, 86, 140); }
NSColor* blueBot()  { return rgb (43, 66, 114); }

NSFont* boldFont (CGFloat size)
{
    NSFont* f = [NSFont fontWithName: @"Helvetica-Bold" size: size];
    return f ? f : [NSFont boldSystemFontOfSize: size];
}

NSFont* boldObliqueFont (CGFloat size)
{
    NSFont* f = [NSFont fontWithName: @"Helvetica-BoldOblique" size: size];
    return f ? f : boldFont (size);
}

NSDictionary* textAttrs (NSFont* font, NSColor* color, CGFloat kern)
{
    return [NSDictionary dictionaryWithObjectsAndKeys:
              font, NSFontAttributeName, color, NSForegroundColorAttributeName,
              [NSNumber numberWithDouble: kern], NSKernAttributeName, nil];
}

NSSize textSize (const char* s, NSFont* font, CGFloat kern = 0.0)
{
    return [[NSString stringWithUTF8String: s] sizeWithAttributes: textAttrs (font, ink(), kern)];
}

void drawText (const char* s, NSFont* font, NSColor* color, CGFloat cx, CGFloat cy, CGFloat kern = 0.0)
{
    NSString* str = [NSString stringWithUTF8String: s];
    NSDictionary* attrs = textAttrs (font, color, kern);
    const NSSize sz = [str sizeWithAttributes: attrs];
    [str drawAtPoint: NSMakePoint (cx - (sz.width - kern) * 0.5, cy - sz.height * 0.5) withAttributes: attrs];
}

void line (CGFloat x0, CGFloat y0, CGFloat x1, CGFloat y1, CGFloat w)
{
    NSBezierPath* p = [NSBezierPath bezierPath];
    [p moveToPoint: NSMakePoint (x0, y0)];
    [p lineToPoint: NSMakePoint (x1, y1)];
    [p setLineWidth: w];
    [p stroke];
}

// Tick marks round a knob: n ticks over the given angles
void drawTicks (CGFloat cx, CGFloat cy, int n, CGFloat a0deg, CGFloat a1deg)
{
    [ink() set];
    for (int k = 0; k < n; ++k)
    {
        const CGFloat a = (CGFloat) ((a0deg + (a1deg - a0deg) * k / (n - 1)) * M_PI / 180.0);
        NSBezierPath* t = [NSBezierPath bezierPath];
        [t moveToPoint: polar (cx, cy, kKnobR + 5.0, a)];
        [t lineToPoint: polar (cx, cy, kKnobR + 11.0, a)];
        [t setLineWidth: 1.6];
        [t stroke];
    }
}

// The small end-of-scale labels, just outside the scale's end ticks
void endLabel (const char* s, CGFloat cx, CGFloat cy, bool left)
{
    NSFont* f = boldFont (8.5);
    const NSSize sz = textSize (s, f);
    const CGFloat ly = cy + 24.0;
    const CGFloat tx = left ? cx - 20.0 - sz.width * 0.5 : cx + 20.0 + sz.width * 0.5;
    drawText (s, f, ink(), tx, ly);
}

} // namespace

//==============================================================================
@interface TBEffectronPanelView : NSView
{
    EditorHost* host;
    float shown[kNumParams];
    float meter[kNumMeters];
    NSTimer* timer;
    int dragCtl;
    CGFloat dragStartY, dragStartValue;
    int readoutParam;
    bool saveMode;
    int activePreset;
    bool presetUsed[kNumPresetSlots];
    char status[64];
    NSImage* backdrop;    // ears, face and fixed lettering, made once
}
- (id) initWithFrame: (NSRect) frame host: (EditorHost*) h;
- (void) shutdown;
@end

@implementation TBEffectronPanelView

- (id) initWithFrame: (NSRect) frame host: (EditorHost*) h
{
    self = [super initWithFrame: frame];
    if (self)
    {
        host = h;
        for (int i = 0; i < kNumParams; ++i) shown[i] = host->getParam (i);
        for (int i = 0; i < kNumMeters; ++i) meter[i] = host->getMeter (i);
        dragCtl = -1;
        readoutParam = -1;
        saveMode = false;
        activePreset = -1;
        status[0] = 0;
        backdrop = nil;
        host->refreshPresets();
        for (int k = 0; k < kNumPresetSlots; ++k) presetUsed[k] = host->presetExists (k);
        timer = [[NSTimer timerWithTimeInterval: 1.0 / 20.0 target: self selector: @selector (tick:)
                                       userInfo: nil repeats: YES] retain];
        [[NSRunLoop currentRunLoop] addTimer: timer forMode: NSDefaultRunLoopMode];
        [[NSRunLoop currentRunLoop] addTimer: timer forMode: NSEventTrackingRunLoopMode];
    }
    return self;
}

- (void) shutdown
{
    if (timer) { [timer invalidate]; [timer release]; timer = nil; }
    if (backdrop) { [backdrop release]; backdrop = nil; }
    host = 0;
}

- (void) dealloc
{
    [self shutdown];
    [super dealloc];
}

- (BOOL) isFlipped { return YES; }
- (BOOL) isOpaque { return YES; }
- (BOOL) acceptsFirstMouse: (NSEvent*) e { (void) e; return YES; }

// Host-side changes (automation, presets) and the LIMIT LED redraw the panel.
- (void) tick: (NSTimer*) t
{
    (void) t;
    if (! host) return;
    bool changed = false, lamps = false;
    for (int i = 0; i < kNumParams; ++i)
    {
        const float v = host->getParam (i);
        if (v != shown[i]) { shown[i] = v; changed = true; }
    }
    for (int i = 0; i < kNumMeters; ++i)
    {
        const float v = host->getMeter (i);
        if (std::fabs (v - meter[i]) > 0.02f) { meter[i] = v; lamps = true; }
    }
    if (changed) [self setNeedsDisplay: YES];
    else if (lamps) [self setNeedsDisplayInRect: NSMakeRect (kLedLimit.x - 20.0, kLedLimit.y - 20.0, 40.0, 40.0)];
}

//------------------------------------------------------------------------------
// Drawing

- (void) drawScrewAt: (NSPoint) c radius: (CGFloat) r
{
    NSBezierPath* head = [NSBezierPath bezierPathWithOvalInRect: NSMakeRect (c.x - r, c.y - r, 2 * r, 2 * r)];
    [NSGraphicsContext saveGraphicsState];
    NSShadow* sh = [[[NSShadow alloc] init] autorelease];
    [sh setShadowOffset: NSMakeSize (1.0, -1.5)];
    [sh setShadowBlurRadius: 2.5];
    [sh setShadowColor: [NSColor colorWithCalibratedWhite: 0.0 alpha: 0.6]];
    [sh set];
    [rgb (30, 29, 28) set];
    [head fill];
    [NSGraphicsContext restoreGraphicsState];
    NSGradient* g = [[[NSGradient alloc] initWithStartingColor: rgb (92, 90, 86) endingColor: rgb (18, 18, 17)] autorelease];
    [g drawInBezierPath: head relativeCenterPosition: NSMakePoint (-0.35, 0.4)];
    [rgb (8, 8, 8) set];
    [head setLineWidth: 0.8];
    [head stroke];
    // Phillips cross
    const CGFloat k = r * 0.55;
    [rgb (6, 6, 6) set];
    line (c.x - k, c.y, c.x + k, c.y, r * 0.22);
    line (c.x, c.y - k, c.x, c.y + k, r * 0.22);
}

// Ears, the blue face and everything printed on it, drawn once
- (NSImage*) backdropImage
{
    if (backdrop) return backdrop;
    const CGFloat w = [self bounds].size.width;
    const CGFloat h = [self bounds].size.height;
    backdrop = [[NSImage alloc] initWithSize: NSMakeSize (w, h)];
    [backdrop lockFocusFlipped: YES];

    // The rack: dark, faintly textured metal behind everything
    [rgb (44, 41, 36) set];
    NSRectFill (NSMakeRect (0, 0, w, h));
    unsigned seed = 1020u;
    for (int k = 0; k < 15000; ++k)
    {
        seed = seed * 1664525u + 1013904223u; const CGFloat x = (seed >> 8) / 16777216.0 * w;
        seed = seed * 1664525u + 1013904223u; const CGFloat y = (seed >> 8) / 16777216.0 * h;
        seed = seed * 1664525u + 1013904223u; const double q = (seed >> 8) / 16777216.0;
        [[NSColor colorWithCalibratedWhite: q < 0.5 ? 1.0 : 0.0 alpha: 0.05 + 0.08 * q] set];
        NSRectFillUsingOperation (NSMakeRect (x, y, 1.0, 1.0), NSCompositeSourceOver);
    }

    // The blue face: one panel, controls on top, presets and readout below
    const NSRect face = NSMakeRect (kEarW, kBlueTop, w - 2 * kEarW, h - kBlueTop - kBlueMargin);
    NSGradient* fg = [[[NSGradient alloc] initWithStartingColor: blueTop() endingColor: blueBot()] autorelease];
    [fg drawInRect: face angle: 90.0];
    // A soft sheen across the middle, as in the photo
    NSGradient* sheen = [[[NSGradient alloc] initWithColorsAndLocations:
                            [NSColor colorWithCalibratedWhite: 1.0 alpha: 0.0], 0.0,
                            [NSColor colorWithCalibratedWhite: 1.0 alpha: 0.05], 0.55,
                            [NSColor colorWithCalibratedWhite: 1.0 alpha: 0.0], 1.0, nil] autorelease];
    [sheen drawInRect: face angle: 0.0];
    [[NSColor colorWithCalibratedWhite: 0.0 alpha: 0.45] set];
    NSFrameRectWithWidth (face, 1.0);
    [[NSColor colorWithCalibratedWhite: 1.0 alpha: 0.12] set];
    NSRectFillUsingOperation (NSMakeRect (face.origin.x + 1, face.origin.y + 1, face.size.width - 2, 1.0), NSCompositeSourceOver);

    // Rack screws (big, on the ears) and the face's fixing screws (small, left end)
    [self drawScrewAt: NSMakePoint (34, 34) radius: 12.5];
    [self drawScrewAt: NSMakePoint (34, h - 34) radius: 12.5];
    [self drawScrewAt: NSMakePoint (w - 34, 32) radius: 12.5];
    [self drawScrewAt: NSMakePoint (w - 34, h - 34) radius: 12.5];
    [self drawScrewAt: NSMakePoint (108, 43) radius: 6.5];
    [self drawScrewAt: NSMakePoint (108, 99) radius: 6.5];

    NSColor* white = ink();

    // INPUT: LEVEL, LIMIT
    drawText ("INPUT", boldFont (12.0), white, 212, 26, 0.6);
    drawText ("LEVEL", boldFont (13.0), white, 212, 112, 0.6);
    drawTicks (211, 75, 11, -150, 150);
    endLabel ("0", 211, 75, true);
    endLabel ("MAX", 211, 75, false);
    drawText ("LIMIT", boldFont (8.5), white, kLedLimit.x, kLedLimit.y + 14.0, 0.3);

    // FEEDBACK: NEG .. 0 .. POS
    drawText ("FEEDBACK", boldFont (13.0), white, 351, 112, 0.6);
    drawTicks (349, 75, 11, -150, 150);
    {
        const NSPoint p = polar (349, 75, kKnobR + 17.0, 0.0);
        [white set];
        NSBezierPath* o = [NSBezierPath bezierPathWithOvalInRect: NSMakeRect (p.x - 3.2, p.y - 3.8, 6.4, 7.6)];
        [o setLineWidth: 1.3];
        [o stroke];
    }
    endLabel ("NEG", 349, 75, true);
    endLabel ("POS", 349, 75, false);

    // EFFECTRON I: italic outline lettering over fine horizontal stripes
    {
        const CGFloat x0 = 425.0, x1 = 638.0, yc = 38.0;
        [[NSColor colorWithCalibratedWhite: 0.85 alpha: 0.8] set];
        for (int k = 0; k < 5; ++k)
            NSRectFillUsingOperation (NSMakeRect (x0, yc - 10.0 + k * 4.5, x1 - x0, 1.4), NSCompositeSourceOver);
        NSFont* f = boldObliqueFont (27.0);
        NSString* s = @"EFFECTRON I";
        NSDictionary* fill = textAttrs (f, rgb (205, 210, 220), 2.0);
        const NSSize sz = [s sizeWithAttributes: fill];
        const NSPoint at = NSMakePoint ((x0 + x1) * 0.5 - sz.width * 0.5 + 6.0, yc - sz.height * 0.5);
        // a blue keyline so the letters stand off the stripes
        NSDictionary* key = [NSDictionary dictionaryWithObjectsAndKeys:
                               f, NSFontAttributeName, blueTop(), NSStrokeColorAttributeName,
                               [NSNumber numberWithDouble: 9.0], NSStrokeWidthAttributeName,
                               [NSNumber numberWithDouble: 2.0], NSKernAttributeName, nil];
        [s drawAtPoint: at withAttributes: key];
        [s drawAtPoint: at withAttributes: fill];
    }

    // DIGITAL DELAY: range names, button-position legend, times in ms
    {
        const char* const names[4] = { "LG ECHO", "SH ECHO", "DOUBLE", "FLANGE" };
        const char* const times[4] = { "256 - 1024", "64 - 256", "16 - 64", "4 - 16" };
        const CGFloat rows[4] = { 67.0, 78.0, 89.0, 99.0 };
        // which buttons are in, per row (LG: both, SH: left, DOUBLE: right, FLANGE: none)
        const bool lIn[4] = { true, true, false, false };
        const bool rIn[4] = { true, false, true, false };
        for (int k = 0; k < 4; ++k)
        {
            drawText (names[k], boldFont (8.5), white, 469, rows[k], 0.2);
            drawText (times[k], boldFont (8.5), white, 622, rows[k], 0.2);
            for (int b = 0; b < 2; ++b)
            {
                const bool in = b == 0 ? lIn[k] : rIn[k];
                const CGFloat cx = b == 0 ? 533.0 : 552.0, cy = rows[k];
                [white set];
                if (in)
                {
                    // pressed in: a flat cap on its base
                    NSRectFill (NSMakeRect (cx - 5.0, cy + 1.5, 10.0, 1.4));
                    NSBezierPath* cap = [NSBezierPath bezierPath];
                    [cap moveToPoint: NSMakePoint (cx - 4.0, cy + 1.5)];
                    [cap lineToPoint: NSMakePoint (cx - 2.8, cy - 1.0)];
                    [cap lineToPoint: NSMakePoint (cx + 2.8, cy - 1.0)];
                    [cap lineToPoint: NSMakePoint (cx + 4.0, cy + 1.5)];
                    [cap closePath];
                    [cap fill];
                }
                else
                {
                    // out: an open button standing up from its base
                    NSRectFill (NSMakeRect (cx - 5.0, cy + 2.5, 10.0, 1.2));
                    NSBezierPath* b2 = [NSBezierPath bezierPathWithRect: NSMakeRect (cx - 2.6, cy - 3.0, 5.2, 5.5)];
                    [b2 setLineWidth: 1.0];
                    [b2 stroke];
                }
            }
        }
        // the bracket under the section
        [white set];
        line (427, 104, 427, 111, 1.6);
        line (427, 111, 455, 111, 1.6);
        line (593, 111, 640, 111, 1.6);
        line (640, 104, 640, 111, 1.6);
        drawText ("DIGITAL DELAY (ms)", boldFont (12.5), white, 524, 112, 0.5);
    }

    // MODULATOR: outline round DELAY / WIDTH and SPEED
    {
        [white set];
        line (729, 25, 752, 25, 1.6);
        line (884, 25, 951, 25, 1.6);
        line (951, 25, 951, 111, 1.6);
        line (951, 111, 923, 111, 1.6);
        line (867, 111, 792, 111, 1.6);
        line (729, 25, 729, 33, 1.6);
        line (733, 99, 733, 111, 1.6);       // from the knob down to WIDTH
        line (733, 111, 739, 111, 1.6);
        drawText ("MODULATOR", boldFont (12.0), white, 818, 26, 0.6);

        drawTicks (733, 75, 11, -150, 150);
        drawText ("MIN / 0%", boldFont (8.0), white, 734, 34, 0.2);
        endLabel ("MAX", 733, 75, true);
        endLabel ("100%", 733, 75, false);
        drawText ("DELAY", boldFont (13.0), white, 698, 112, 0.6);
        drawText ("WIDTH", boldFont (13.0), white, 762, 112, 0.6);

        drawTicks (895, 75, 11, -150, 150);
        endLabel ("SLOW", 895, 75, true);
        endLabel ("FAST", 895, 75, false);
        drawText ("SPEED", boldFont (13.0), white, 895, 112, 0.6);
    }

    // OUTPUT: MIX, SOURCE .. EQUAL .. EFFECT
    drawText ("OUTPUT", boldFont (12.0), white, 1049, 24, 0.6);
    drawText ("EQUAL", boldFont (8.0), white, 1049, 35, 0.2);
    drawTicks (1049, 75, 11, -150, 150);
    endLabel ("SOURCE", 1049, 75, true);
    endLabel ("EFFECT", 1049, 75, false);
    drawText ("MIX", boldFont (13.0), white, 1049, 112, 0.6);

    // REPEAT (the rear footswitch's infinite repeat, on the face here)
    drawText ("REPEAT", boldFont (8.5), white, 72, 50, 0.3);

    // The TB electronic logo, red and black, at the right-hand end; the model number under it
    tblogo::drawTBLogo (kLogoBox);
    drawText ("ADM 1020", boldObliqueFont (13.5), white, 1160, 104, 0.8);

    [backdrop unlockFocus];
    return backdrop;
}

- (void) drawBackground
{
    const NSRect b = [self bounds];
    [[self backdropImage] drawInRect: b fromRect: NSZeroRect
                           operation: NSCompositeSourceOver fraction: 1.0 respectFlipped: YES hints: nil];
}

// Preset buttons in the panel's red: dull when empty, the unit's red when
// saved, lit with a red glow when in use (or SAVE while armed).
- (void) drawRedKey: (NSRect) r state: (int) state
{
    NSBezierPath* bp = [NSBezierPath bezierPathWithRect: r];
    if (state == 2)
    {
        [NSGraphicsContext saveGraphicsState];
        NSShadow* glow = [[[NSShadow alloc] init] autorelease];
        [glow setShadowOffset: NSMakeSize (0.0, 0.0)];
        [glow setShadowBlurRadius: 12.0];
        [glow setShadowColor: [NSColor colorWithCalibratedRed: 1.0 green: 0.28 blue: 0.16 alpha: 1.0]];
        [glow set];
        [rgb (232, 84, 58) set];
        [bp fill];
        [NSGraphicsContext restoreGraphicsState];
        NSGradient* g = [[[NSGradient alloc] initWithColorsAndLocations:
                            rgb (255, 196, 168), 0.0, rgb (246, 112, 82), 0.4, rgb (214, 70, 48), 0.85, rgb (176, 52, 36), 1.0, nil] autorelease];
        [g drawInBezierPath: bp relativeCenterPosition: NSMakePoint (0.0, 0.0)];
    }
    else
    {
        [NSGraphicsContext saveGraphicsState];
        NSShadow* sh = [[[NSShadow alloc] init] autorelease];
        [sh setShadowOffset: NSMakeSize (1.0, -2.0)];
        [sh setShadowBlurRadius: 2.5];
        [sh setShadowColor: [NSColor colorWithCalibratedWhite: 0.0 alpha: 0.7]];
        [sh set];
        [rgb (130, 54, 44) set];
        [bp fill];
        [NSGraphicsContext restoreGraphicsState];
        NSGradient* g = state == 1
            ? [[[NSGradient alloc] initWithStartingColor: rgb (176, 80, 64) endingColor: rgb (122, 48, 38)] autorelease]
            : [[[NSGradient alloc] initWithStartingColor: rgb (120, 56, 50) endingColor: rgb (84, 36, 30)] autorelease];
        [g drawInBezierPath: bp angle: -90.0];
        [[NSColor colorWithCalibratedWhite: 1.0 alpha: 0.16] set];
        NSRectFillUsingOperation (NSMakeRect (NSMinX (r) + 2.0, NSMinY (r) + 1.5, r.size.width - 4.0, 1.2), NSCompositeSourceOver);
    }
    [rgb (12, 12, 14) set];
    [bp setLineWidth: 1.0];
    [bp stroke];
}

// Knurled black skirt; a teal cap with a white line.
// skirtPointer: the line is on the skirt's edge (DELAY's outer ring) rather than the cap.
- (void) drawKnobAt: (NSPoint) c skirtValue: (CGFloat) sv capAngle: (CGFloat) capA
            tealCap: (bool) teal skirtPointer: (bool) skirtPointer
{
    const CGFloat r = kKnobR;
    const CGFloat sa = valueAngle (sv);

    // Shadow and the knurled skirt (turns with the knob)
    NSBezierPath* skirt = [NSBezierPath bezierPath];
    const int ridges = 36;
    for (int k = 0; k <= ridges * 4; ++k)
    {
        const CGFloat t = (CGFloat) (2.0 * M_PI * k / (ridges * 4));
        const CGFloat rr = r * (1.0 - 0.045 * (0.5 - 0.5 * std::cos (ridges * t)));
        const NSPoint p = polar (c.x, c.y, rr, t + sa);
        if (k == 0) [skirt moveToPoint: p]; else [skirt lineToPoint: p];
    }
    [skirt closePath];
    [NSGraphicsContext saveGraphicsState];
    NSShadow* sh = [[[NSShadow alloc] init] autorelease];
    [sh setShadowOffset: NSMakeSize (1.5, -3.5)];
    [sh setShadowBlurRadius: 5.0];
    [sh setShadowColor: [NSColor colorWithCalibratedWhite: 0.0 alpha: 0.7]];
    [sh set];
    [rgb (20, 18, 16) set];
    [skirt fill];
    [NSGraphicsContext restoreGraphicsState];
    NSGradient* sk = [[[NSGradient alloc] initWithColorsAndLocations:
                         rgb (92, 86, 80), 0.0, rgb (46, 42, 38), 0.45, rgb (16, 14, 12), 1.0, nil] autorelease];
    [sk drawInBezierPath: skirt relativeCenterPosition: NSMakePoint (-0.35, 0.5)];
    // the knurl's ridges
    [[NSColor colorWithCalibratedWhite: 0.0 alpha: 0.55] set];
    for (int k = 0; k < ridges; ++k)
    {
        const CGFloat t = (CGFloat) (2.0 * M_PI * k / ridges) + sa;
        line (polar (c.x, c.y, r * 0.80, t).x, polar (c.x, c.y, r * 0.80, t).y,
              polar (c.x, c.y, r * 0.97, t).x, polar (c.x, c.y, r * 0.97, t).y, 0.8);
    }
    if (skirtPointer)
    {
        [ink() set];
        NSBezierPath* p = [NSBezierPath bezierPath];
        [p moveToPoint: polar (c.x, c.y, kCapR + 1.5, sa)];
        [p lineToPoint: polar (c.x, c.y, r * 0.98, sa)];
        [p setLineWidth: 2.4];
        [p setLineCapStyle: NSRoundLineCapStyle];
        [p stroke];
    }

    // Cap
    const CGFloat rc = kCapR;
    NSBezierPath* cap = [NSBezierPath bezierPathWithOvalInRect: NSMakeRect (c.x - rc, c.y - rc, 2 * rc, 2 * rc)];
    [NSGraphicsContext saveGraphicsState];
    NSShadow* cs = [[[NSShadow alloc] init] autorelease];
    [cs setShadowOffset: NSMakeSize (0.8, -1.5)];
    [cs setShadowBlurRadius: 2.0];
    [cs setShadowColor: [NSColor colorWithCalibratedWhite: 0.0 alpha: 0.6]];
    [cs set];
    [rgb (20, 20, 20) set];
    [cap fill];
    [NSGraphicsContext restoreGraphicsState];
    if (teal)
    {
        NSGradient* cg = [[[NSGradient alloc] initWithColorsAndLocations:
                             rgb (126, 162, 176), 0.0, rgb (84, 118, 136), 0.5, rgb (56, 84, 102), 1.0, nil] autorelease];
        [cg drawInBezierPath: cap relativeCenterPosition: NSMakePoint (-0.3, 0.45)];
    }
    else
    {
        // SPEED: black cap with a shallow dished ring
        NSGradient* cg = [[[NSGradient alloc] initWithColorsAndLocations:
                             rgb (70, 66, 62), 0.0, rgb (34, 31, 28), 0.6, rgb (14, 13, 12), 1.0, nil] autorelease];
        [cg drawInBezierPath: cap relativeCenterPosition: NSMakePoint (-0.3, 0.45)];
        const CGFloat ri = rc * 0.62;
        NSBezierPath* dish = [NSBezierPath bezierPathWithOvalInRect: NSMakeRect (c.x - ri, c.y - ri, 2 * ri, 2 * ri)];
        NSGradient* dg = [[[NSGradient alloc] initWithStartingColor: rgb (10, 9, 8) endingColor: rgb (58, 54, 50)] autorelease];
        [dg drawInBezierPath: dish angle: -60.0];
    }
    [[NSColor colorWithCalibratedWhite: 0.0 alpha: 0.6] set];
    [cap setLineWidth: 0.8];
    [cap stroke];

    {
        [ink() set];
        NSBezierPath* p = [NSBezierPath bezierPath];
        [p moveToPoint: polar (c.x, c.y, teal ? 1.0 : rc * 0.62, capA)];
        [p lineToPoint: polar (c.x, c.y, rc * 0.92, capA)];
        [p setLineWidth: 2.2];
        [p setLineCapStyle: NSRoundLineCapStyle];
        [p stroke];
    }
}

// Red push button. In: pressed down and lit from inside with a red glow round
// it; out: standing proud, dull and unlit.
- (void) drawButtonAt: (NSPoint) c in: (bool) in
{
    const NSRect bezel = NSMakeRect (c.x - kButtonHalfW - 3.0, c.y - kButtonHalfH - 3.0, 2 * kButtonHalfW + 6.0, 2 * kButtonHalfH + 6.0);
    NSBezierPath* bp = [NSBezierPath bezierPathWithRoundedRect: bezel xRadius: 2.5 yRadius: 2.5];
    [rgb (12, 12, 14) set];
    [bp fill];

    const CGFloat sink = in ? 1.5 : 0.0;
    const NSRect cap = NSMakeRect (c.x - kButtonHalfW + (in ? 0.8 : 0.0), c.y - kButtonHalfH + sink,
                                   2 * kButtonHalfW - (in ? 1.6 : 0.0), 2 * kButtonHalfH - (in ? 1.6 : 0.0) - sink * 0.5);
    NSBezierPath* cp = [NSBezierPath bezierPathWithRoundedRect: cap xRadius: 2.0 yRadius: 2.0];
    if (in)
    {
        [NSGraphicsContext saveGraphicsState];
        NSShadow* glow = [[[NSShadow alloc] init] autorelease];
        [glow setShadowOffset: NSMakeSize (0.0, 0.0)];
        [glow setShadowBlurRadius: 12.0];
        [glow setShadowColor: [NSColor colorWithCalibratedRed: 1.0 green: 0.28 blue: 0.16 alpha: 1.0]];
        [glow set];
        [rgb (232, 84, 58) set];
        [cp fill];
        [cp fill];
        [NSGraphicsContext restoreGraphicsState];
        NSGradient* g = [[[NSGradient alloc] initWithColorsAndLocations:
                            rgb (255, 196, 168), 0.0, rgb (246, 112, 82), 0.35, rgb (214, 70, 48), 0.8, rgb (176, 52, 36), 1.0, nil] autorelease];
        [g drawInBezierPath: cp relativeCenterPosition: NSMakePoint (0.0, 0.0)];
    }
    else
    {
        [NSGraphicsContext saveGraphicsState];
        NSShadow* sh = [[[NSShadow alloc] init] autorelease];
        [sh setShadowOffset: NSMakeSize (1.0, -2.0)];
        [sh setShadowBlurRadius: 2.5];
        [sh setShadowColor: [NSColor colorWithCalibratedWhite: 0.0 alpha: 0.8]];
        [sh set];
        [rgb (150, 64, 52) set];
        [cp fill];
        [NSGraphicsContext restoreGraphicsState];
        NSGradient* g = [[[NSGradient alloc] initWithStartingColor: rgb (176, 80, 64) endingColor: rgb (122, 48, 38)] autorelease];
        [g drawInBezierPath: cp angle: -90.0];
        [[NSColor colorWithCalibratedWhite: 1.0 alpha: 0.18] set];
        NSRectFillUsingOperation (NSMakeRect (NSMinX (cap) + 2.0, NSMinY (cap) + 1.5, cap.size.width - 4.0, 1.2), NSCompositeSourceOver);
    }
    [[NSColor colorWithCalibratedWhite: 0.0 alpha: 0.6] set];
    [cp setLineWidth: 0.8];
    [cp stroke];
}

- (void) drawLimitLed
{
    const CGFloat lit = meter[kMeterLimit] > 2.0f ? 1.0 : 0.0;   // Effectron::limitThreshold()
    const NSPoint c = kLedLimit;
    if (lit > 0.0)
    {
        const CGFloat hr = 14.0;
        NSGradient* halo = [[[NSGradient alloc] initWithColorsAndLocations:
                               [NSColor colorWithCalibratedRed: 1.0 green: 0.15 blue: 0.1 alpha: 0.75], 0.0,
                               [NSColor colorWithCalibratedRed: 1.0 green: 0.15 blue: 0.1 alpha: 0.2], 0.5,
                               [NSColor colorWithCalibratedRed: 1.0 green: 0.15 blue: 0.1 alpha: 0.0], 1.0, nil] autorelease];
        [halo drawInBezierPath: [NSBezierPath bezierPathWithOvalInRect: NSMakeRect (c.x - hr, c.y - hr, 2 * hr, 2 * hr)]
              relativeCenterPosition: NSMakePoint (0.0, 0.0)];
    }
    const CGFloat rb = 5.5, rl = 3.8;
    [rgb (16, 18, 26) set];
    [[NSBezierPath bezierPathWithOvalInRect: NSMakeRect (c.x - rb, c.y - rb, 2 * rb, 2 * rb)] fill];
    NSBezierPath* lens = [NSBezierPath bezierPathWithOvalInRect: NSMakeRect (c.x - rl, c.y - rl, 2 * rl, 2 * rl)];
    NSGradient* lg = lit > 0.0
        ? [[[NSGradient alloc] initWithStartingColor: rgb (255, 190, 170) endingColor: rgb (240, 30, 20)] autorelease]
        : [[[NSGradient alloc] initWithStartingColor: rgb (150, 70, 90) endingColor: rgb (70, 30, 50)] autorelease];
    [lg drawInBezierPath: lens relativeCenterPosition: NSMakePoint (-0.3, 0.4)];
}

// Grey knurled knob with a red position line (the strip's trims).
- (void) drawTrimAt: (NSPoint) ctr radius: (CGFloat) r value: (CGFloat) v
{
    [NSGraphicsContext saveGraphicsState];
    NSShadow* sh = [[[NSShadow alloc] init] autorelease];
    [sh setShadowOffset: NSMakeSize (1.5, -2.5)];
    [sh setShadowBlurRadius: 4.0];
    [sh setShadowColor: [NSColor colorWithCalibratedWhite: 0.0 alpha: 0.45]];
    [sh set];
    [rgb (90, 93, 97) set];
    [[NSBezierPath bezierPathWithOvalInRect: NSMakeRect (ctr.x - r, ctr.y - r, 2 * r, 2 * r)] fill];
    [NSGraphicsContext restoreGraphicsState];

    const int ridges = 40;
    for (int k = 0; k < ridges; ++k)
    {
        const CGFloat a0 = (CGFloat) (2.0 * M_PI * k / ridges);
        const CGFloat a1 = (CGFloat) (2.0 * M_PI * (k + 0.5) / ridges);
        NSBezierPath* w = [NSBezierPath bezierPath];
        [w moveToPoint: ctr];
        [w lineToPoint: polar (ctr.x, ctr.y, r, a0)];
        [w lineToPoint: polar (ctr.x, ctr.y, r, a1)];
        [w closePath];
        const CGFloat lit = 0.5 + 0.5 * std::cos (a0 + 0.8);
        [[NSColor colorWithCalibratedWhite: 0.40 + 0.22 * lit alpha: 1.0] set];
        [w fill];
    }

    const CGFloat rc = r * 0.78;
    NSGradient* cap = [[[NSGradient alloc] initWithStartingColor: rgb (172, 175, 179)
                                                     endingColor: rgb (104, 107, 111)] autorelease];
    NSBezierPath* capPath = [NSBezierPath bezierPathWithOvalInRect: NSMakeRect (ctr.x - rc, ctr.y - rc, 2 * rc, 2 * rc)];
    [cap drawInBezierPath: capPath angle: -60.0];
    [rgb (70, 72, 75) set];
    [capPath setLineWidth: 0.8];
    [capPath stroke];

    const CGFloat a = valueAngle (v);
    NSBezierPath* ln = [NSBezierPath bezierPath];
    [ln moveToPoint: polar (ctr.x, ctr.y, r * 0.18, a)];
    [ln lineToPoint: polar (ctr.x, ctr.y, r * 0.98, a)];
    [ln setLineWidth: 2.4];
    [ln setLineCapStyle: NSRoundLineCapStyle];
    [rgb (200, 30, 30) set];
    [ln stroke];
}

- (void) drawReadout
{
    const NSRect box = kReadoutRect;
    NSBezierPath* p = [NSBezierPath bezierPathWithRect: box];
    NSGradient* glow = [[[NSGradient alloc] initWithStartingColor: rgb (206, 255, 120)
                                                      endingColor: rgb (150, 232, 70)] autorelease];
    [glow drawInBezierPath: p relativeCenterPosition: NSMakePoint (0.0, 0.0)];
    [rgb (60, 62, 65) set];
    [p setLineWidth: 1.0];
    [p stroke];

    if (status[0])
    {
        drawText (status, boldFont (16.0), [NSColor blackColor], NSMidX (box), NSMidY (box));
    }
    else if (readoutParam >= 0 && host)
    {
        char disp[16] = { 0 };
        host->getParamDisplay (readoutParam, disp);
        char text[96];
        const char* name = 0;
        for (int i = 0; i < kNumCtls; ++i)
            if (kCtls[i].param == readoutParam) { name = kCtls[i].label; break; }
        const int kind = kParamKind[readoutParam];
        const char* unit = "";
        if (kind == kSpeedHz) unit = " Hz";
        else if (kind == kDb12 || kind == kDb24) unit = " dB";
        else if (kind == kPercent || kind == kBipolar || kind == kMixPct) unit = " %";
        else if (kind == kDelayMs && std::strncmp (disp, "Env", 3) != 0) unit = " ms";
        if (kind == kDelayMs && std::strncmp (disp, "Env", 3) == 0)
            std::snprintf (text, sizeof (text), "DELAY   ENVELOPE %s %%", disp + 4);
        else if (kind == kRangeSel)
        {
            static const char* const names[4] = { "FLANGE  4 - 16 ms", "DOUBLE  16 - 64 ms", "SH ECHO  64 - 256 ms", "LG ECHO  256 - 1024 ms" };
            std::snprintf (text, sizeof (text), "DIGITAL DELAY   %s", names[rangeIndex (shown[kRange])]);
        }
        else if (kind == kMixPct)
        {
            const float v = shown[kMix];
            std::snprintf (text, sizeof (text), "MIX   %s", v < 0.02f ? "SOURCE" : (v > 0.98f ? "EFFECT" : (std::fabs (v - 0.5f) < 0.02f ? "EQUAL" : disp)));
            if (v >= 0.02f && v <= 0.98f && std::fabs (v - 0.5f) >= 0.02f)
                std::snprintf (text, sizeof (text), "MIX   %s %% EFFECT", disp);
        }
        else
            std::snprintf (text, sizeof (text), "%s   %s%s", name ? name : "", disp, unit);
        drawText (text, boldFont (16.0), [NSColor blackColor], NSMidX (box), NSMidY (box));
    }
}

- (void) drawPresets
{
    drawText ("PRESETS", boldFont (10.5), ink(), (NSMinX (kSaveRect) + NSMaxX (slotRect (kNumPresetSlots - 1))) * 0.5, kStripLabelY, 0.5);

    [self drawRedKey: kSaveRect state: saveMode ? 2 : 1];
    drawText ("SAVE", boldFont (11.0), saveMode ? rgb (60, 10, 6) : [NSColor whiteColor], NSMidX (kSaveRect), NSMidY (kSaveRect));

    for (int k = 0; k < kNumPresetSlots; ++k)
    {
        const NSRect r = slotRect (k);
        const int state = (k == activePreset) ? 2 : (presetUsed[k] ? 1 : 0);
        [self drawRedKey: r state: state];
        NSColor* text = state == 2 ? rgb (60, 10, 6) : (state == 1 ? [NSColor whiteColor] : rgb (196, 140, 130));
        char num[4];
        std::snprintf (num, sizeof (num), "%d", k + 1);
        drawText (num, boldFont (12.0), text, NSMidX (r), NSMidY (r));
    }
}

- (void) drawRect: (NSRect) dirty
{
    (void) dirty;
    [self drawBackground];
    const int range = rangeIndex (shown[kRange]);

    for (int i = 0; i < kNumCtls; ++i)
    {
        const Ctl& c = kCtls[i];
        const NSPoint p = NSMakePoint (c.cx, c.cy);
        switch (c.type)
        {
            case kCtlKnob:
                [self drawKnobAt: p skirtValue: shown[c.param] capAngle: valueAngle (shown[c.param]) tealCap: true skirtPointer: false];
                break;
            case kCtlOuterRing:
                // DELAY on the black skirt, WIDTH on the teal cap
                [self drawKnobAt: p skirtValue: shown[kDelay] capAngle: widthAngle (shown[kWidth]) tealCap: true skirtPointer: true];
                break;
            case kCtlInnerCap:
                break;      // drawn with the outer ring
            case kCtlRangeButton:
                [self drawButtonAt: p in: c.which == 0 ? leftButtonIn (range) : rightButtonIn (range)];
                break;
            case kCtlPushButton:
                [self drawButtonAt: p in: isOn (shown[c.param])];
                break;
            case kCtlSmallKnob:
                [self drawTrimAt: p radius: c.r value: shown[c.param]];
                drawText (c.label, boldFont (10.5), ink(), c.cx, kStripLabelY, 0.5);
                drawText (c.posA, boldFont (9.0), ink(), c.cx - c.r + 1.0, c.cy + c.r + 9.0);
                drawText (c.posB, boldFont (9.0), ink(), c.cx + c.r - 1.0, c.cy + c.r + 9.0);
                break;
            default: break;
        }
    }
    [self drawLimitLed];
    [self drawPresets];
    [self drawReadout];
}

//------------------------------------------------------------------------------
// Mouse

- (int) ctlAtPoint: (NSPoint) p
{
    for (int i = 0; i < kNumCtls; ++i)
    {
        const Ctl& c = kCtls[i];
        const CGFloat dx = p.x - c.cx, dy = p.y - c.cy;
        if (c.type == kCtlRangeButton || c.type == kCtlPushButton)
        {
            if (std::fabs (dx) <= kButtonHalfW + 4.0 && std::fabs (dy) <= kButtonHalfH + 4.0) return i;
            continue;
        }
        CGFloat reach = c.r + 6.0;
        if (c.type == kCtlInnerCap) reach = c.r;            // listed before the outer ring, so it wins inside
        if (dx * dx + dy * dy < reach * reach) return i;
    }
    return -1;
}

- (void) setParam: (int) param value: (float) v
{
    if (! host) return;
    if (v < 0.0f) v = 0.0f;
    if (v > 1.0f) v = 1.0f;
    host->guiSetParam (param, v);
    shown[param] = host->getParam (param);
    readoutParam = param;
    [self setNeedsDisplay: YES];
}

// SAVE button and preset slots. Returns YES if the click was handled here.
- (BOOL) presetClick: (NSPoint) p
{
    if (NSPointInRect (p, kSaveRect))
    {
        saveMode = ! saveMode;
        std::snprintf (status, sizeof (status), "%s", saveMode ? "SAVE: CLICK A SLOT 1-8" : "");
        [self setNeedsDisplay: YES];
        return YES;
    }
    for (int k = 0; k < kNumPresetSlots; ++k)
    {
        if (! NSPointInRect (p, slotRect (k))) continue;
        if (saveMode)
        {
            const bool ok = host->savePreset (k);
            saveMode = false;
            host->refreshPresets();
            for (int j = 0; j < kNumPresetSlots; ++j) presetUsed[j] = host->presetExists (j);
            if (ok) activePreset = k;
            std::snprintf (status, sizeof (status), ok ? "PRESET %d SAVED" : "COULD NOT SAVE PRESET %d", k + 1);
        }
        else
        {
            host->refreshPresets();
            for (int j = 0; j < kNumPresetSlots; ++j) presetUsed[j] = host->presetExists (j);
            if (presetUsed[k] && host->loadPreset (k))
            {
                for (int j = 0; j < kNumParams; ++j) shown[j] = host->getParam (j);
                activePreset = k;
                std::snprintf (status, sizeof (status), "PRESET %d LOADED", k + 1);
            }
            else
                std::snprintf (status, sizeof (status), "PRESET %d IS EMPTY", k + 1);
        }
        [self setNeedsDisplay: YES];
        return YES;
    }
    return NO;
}

- (void) mouseDown: (NSEvent*) e
{
    const NSPoint p = [self convertPoint: [e locationInWindow] fromView: nil];
    if (! host) return;
    if ([self presetClick: p]) return;
    const int i = [self ctlAtPoint: p];
    if (i < 0) return;
    const Ctl& c = kCtls[i];
    shown[c.param] = host->getParam (c.param);   // the live value, even between timer ticks
    readoutParam = c.param;
    status[0] = 0;
    activePreset = -1;     // settings are being changed away from the preset

    if (c.type == kCtlRangeButton)
    {
        // Each press toggles that button (push on, push off), as on the unit
        const int r = rangeIndex (shown[kRange]);
        bool l = leftButtonIn (r), rr = rightButtonIn (r);
        if (c.which == 0) l = ! l; else rr = ! rr;
        host->guiBeginEdit (kRange);
        [self setParam: kRange value: rangeValue (rangeFromButtons (l, rr))];
        host->guiEndEdit (kRange);
        return;
    }
    if (c.type == kCtlPushButton)
    {
        host->guiBeginEdit (c.param);
        [self setParam: c.param value: isOn (shown[c.param]) ? 0.0f : 1.0f];
        host->guiEndEdit (c.param);
        return;
    }

    host->guiBeginEdit (c.param);
    if ([e clickCount] == 2)
        [self setParam: c.param value: kDefaults[c.param]];   // double-click: back to default
    dragCtl = i;
    dragStartY = p.y;
    dragStartValue = shown[c.param];
    [self setNeedsDisplay: YES];
}

- (void) mouseDragged: (NSEvent*) e
{
    if (dragCtl < 0 || ! host) return;
    const Ctl& c = kCtls[dragCtl];
    const NSPoint p = [self convertPoint: [e locationInWindow] fromView: nil];
    const bool fine = ([e modifierFlags] & NSShiftKeyMask) != 0;
    const CGFloat perPixel = fine ? 1.0 / 1000.0 : 1.0 / 200.0;
    const CGFloat v = dragStartValue + (dragStartY - p.y) * perPixel;
    [self setParam: c.param value: (float) v];
}

- (void) mouseUp: (NSEvent*) e
{
    (void) e;
    if (dragCtl >= 0 && host) host->guiEndEdit (kCtls[dragCtl].param);
    dragCtl = -1;
}

- (void) scrollWheel: (NSEvent*) e
{
    const NSPoint p = [self convertPoint: [e locationInWindow] fromView: nil];
    const int i = [self ctlAtPoint: p];
    if (i < 0 || ! host) return;
    const Ctl& c = kCtls[i];
    if (c.type != kCtlKnob && c.type != kCtlSmallKnob && c.type != kCtlOuterRing && c.type != kCtlInnerCap) return;
    const CGFloat d = [e deltaY];
    if (d == 0.0) return;
    status[0] = 0;
    activePreset = -1;
    host->guiBeginEdit (c.param);
    [self setParam: c.param value: (float) (shown[c.param] + d * 0.01)];
    host->guiEndEdit (c.param);
}

@end

//==============================================================================
namespace tbeffectron
{

// Both calls use their own autorelease pool, so nothing of this plugin's is
// left waiting in the host's pool when the editor closes or the plugin unloads.
void* editorOpen (void* parentView, EditorHost* host)
{
    NSView* parent = (NSView*) parentView;
    if (! parent || ! host) return 0;
    NSAutoreleasePool* pool = [[NSAutoreleasePool alloc] init];
    TBEffectronPanelView* v = [[TBEffectronPanelView alloc]
                                 initWithFrame: NSMakeRect (0, 0, kEditorWidth, kEditorHeight) host: host];
    if (v) [parent addSubview: v];
    [pool release];
    return (void*) v;    // retained until editorClose
}

void editorClose (void* handle)
{
    TBEffectronPanelView* v = (TBEffectronPanelView*) handle;
    if (! v) return;
    NSAutoreleasePool* pool = [[NSAutoreleasePool alloc] init];
    [v shutdown];
    [v removeFromSuperview];
    [v release];
    [pool release];
}

} // namespace tbeffectron
