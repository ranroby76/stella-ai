// C:\workspace\Stella AI Studio\src\LayerDoc.h
// From KnobMaker (C:\workspace\knobmaker); in Stella an Image layer's file can also be a bare
// name in the open project's pictures folder (LayerImages, at the end).

/*
    LayerDoc.h

    The layer document. A canvas, a frame count, and an ordered stack of layers;
    a layer is one primitive plus one effect block.

    Every animatable value is an AnimVal: a from, a to, and a curve. The
    filmstrip is produced by evaluating the whole document at t = 0..1 across
    the frame count. That is what lets one engine make knobs (animate angle),
    sliders (animate offset), buttons (animate brightness) and meters (animate
    a mask) without knowing what any of those are.
*/

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <vector>

//==============================================================================
enum class AnimMode
{
    Fixed = 0,      // always From
    Linear,         // From -> To
    Sine,           // From -> To, eased at both ends
    Reverse,        // To -> From
    Triangle,       // From -> To -> From
    numModes
};

juce::StringArray animModeNames();

//==============================================================================
struct AnimVal
{
    AnimVal() = default;
    AnimVal (float fixed) : from (fixed), to (fixed) {}
    AnimVal (float f, float t, AnimMode m = AnimMode::Linear) : from (f), to (t), mode (m) {}

    float from = 0.0f;
    float to   = 0.0f;
    AnimMode mode = AnimMode::Fixed;

    /** Evaluates at normalised time t (0..1). */
    float at (float t) const noexcept;

    bool isAnimated() const noexcept { return mode != AnimMode::Fixed; }
};

//==============================================================================
enum class PrimType
{
    None = 0,
    Image,
    Circle,
    CircleFill,
    MetalCircle,
    WaveCircle,
    Sphere,
    Rect,
    RectFill,
    Triangle,
    Line,
    RadiateLine,
    HLines,
    VLines,
    Text,
    Shape,

    // Appended, never inserted: the type is serialised as an int, so shifting
    // these values would re-point every layer in every existing document.
    MeterCircle,
    MeterLines,
    Led,

    numTypes
};

juce::StringArray primTypeNames();

//==============================================================================
/** A response curve, 0..1 in and 0..1 out — Photoshop's Gloss Contour. Points
    are held in x order; a segment between two smooth points eases, a segment
    touching a corner point runs straight. */
struct Contour
{
    struct Point
    {
        float x = 0.0f;
        float y = 0.0f;
        bool  corner = false;
    };

    std::vector<Point> points { { 0.0f, 0.0f, true }, { 1.0f, 1.0f, true } };

    float at (float x) const noexcept;

    void addPoint (float x, float y);
    void removePoint (int index);
    void sortPoints();

    juce::String toString() const;
    static Contour fromString (const juce::String&);

    static juce::StringArray presetNames();
    static Contour           preset (int index);
};

//==============================================================================
enum class BevelStyle
{
    OuterBevel = 0,
    InnerBevel,
    Emboss,
    PillowEmboss,
    StrokeEmboss,
    numStyles
};

enum class BevelTechnique { Smooth = 0, ChiselHard, ChiselSoft, numTechniques };

enum class BlendMode { Normal = 0, Multiply, Screen, Overlay, LinearDodge, LinearBurn, numModes };

juce::StringArray bevelStyleNames();
juce::StringArray bevelTechniqueNames();
juce::StringArray blendModeNames();

//==============================================================================
/** Photoshop's Bevel & Emboss. Every style is a band on the signed distance to
    the layer's edge, so it follows any silhouette without knowing its shape. */
struct BevelSpec
{
    bool enabled = false;

    // Structure
    BevelStyle     style     = BevelStyle::InnerBevel;
    BevelTechnique technique = BevelTechnique::Smooth;
    float depth  = 100.0f;   // 1..1000 %
    bool  up     = true;     // Direction
    float size   = 5.0f;     // px at 1x
    float soften = 0.0f;     // px at 1x

    // Shading
    // Contour — Photoshop's sub-item, and the one that changes the bevel's
    // visible form. It reshapes the profile ACROSS the bevel band: a ramp
    // becomes a ring, a double ring, a rounded step. Range decides how much of
    // the band the curve is spread over.
    bool    contourEnabled = false;
    Contour bevelContour;
    float   contourRange = 100.0f;   // 1..100 %

    /** Softens the creases a corner point in either contour puts into the
        height field, which otherwise stair-step once shaded. */
    bool antiAliased = true;

    bool  useGlobalLight = true;
    float angle    = 120.0f;   // azimuth, degrees
    float altitude =  30.0f;   // degrees above the surface

    // Gloss Contour — a different job: it reshapes the LIGHTING response, not
    // the surface, so it changes how metallic or glassy the bevel reads.
    Contour glossContour;

    BlendMode    highlightMode    = BlendMode::Screen;
    juce::Colour highlightColour  { 0xffffffff };
    float        highlightOpacity = 75.0f;

    BlendMode    shadowMode    = BlendMode::Multiply;
    juce::Colour shadowColour  { 0xff000000 };
    float        shadowOpacity = 75.0f;
};

//==============================================================================
/** A drop shadow cast by the layer's own silhouette.

    Size grows the edge outward and nothing else: the interior stays solid, so a
    fluted skirt keeps its flutes rather than swelling into a blob. Fade decides
    how much of that growth is a gradient — 0 gives a hard offset copy, 100 puts
    the falloff right at the silhouette. */
/** Shared by the drop shadow, the inner shadow and the outer glow. All three
    are the same construction — a coverage ramp measured from the silhouette —
    read outward or inward, with or without an offset. */
struct ShadowSpec
{
    bool         enabled = false;
    juce::Colour colour  { 0xff000000 };
    float        opacity = 60.0f;   // 0..100

    float offsetX = 4.0f;    // px at 1x, positive right
    float offsetY = 4.0f;    // px at 1x, positive down
    // How far past the silhouette the shadow reaches, in canvas px at 1x.
    // Bounded in practice by the canvas, not by this number: the shadow lives
    // inside the exported frame, so once its reach touches the frame edges it
    // becomes the frame. A shape of radius R on a canvas of side C has C/2 - R
    // of room before it starts clipping.
    float size    = 6.0f;
    float fade    = 70.0f;   // 0..100 %, how much of that reach is a gradient

    /** Keeps the shadow out from under the layer itself. Only visible when the
        layer is semi-transparent, but then it matters. */
    bool knockout = true;

    /** How the light actually falls away with distance.

        0 leaves the eased ramp everything else uses. Turn it up and the falloff
        becomes inverse-square: hot and tight near the shape with a long faint
        tail, instead of a disc that holds near full and then drops. */
    float curve = 0.0f;      // 0..100

    /** How much the dense end clips toward white.

        This is the part no image editor exposes, and the reason a plain glow
        looks sprayed on. Real light does not fade from one colour to
        transparent — near the source it blows out toward white, further out it
        is fully saturated, further still it darkens away. Holding a single hue
        and only dropping the alpha is what gives the sprayed-paint look. */
    float hotCore = 0.0f;    // 0..100

    /** How it lands on what is already behind it. A shadow wants Normal or
        Multiply; a glow wants Screen, which is what makes it read as light
        being added rather than a coloured disc being laid on top. */
    BlendMode blend = BlendMode::Normal;
};

//==============================================================================
/** Where the outline sits relative to the shape's edge, as Photoshop's Stroke
    layer effect does it. */
enum class StrokePosition { Outside = 0, Centre, Inside, numPositions };

juce::StringArray strokePositionNames();

//==============================================================================
enum class LedShape { Circle = 0, Ellipse, Rect, Triangle, numShapes };

juce::StringArray ledShapeNames();

//==============================================================================
enum class TextureMode
{
    Off = 0,
    Bump,       ///< a height field perturbing the surface normal — KnobMan's own behaviour
    Fill,       ///< the pattern itself shows through, as tone or as colour
    numModes
};

juce::StringArray textureModeNames();

//==============================================================================
struct PrimitiveSpec
{
    PrimType type = PrimType::CircleFill;

    juce::Colour colour { 0xffb8b8b8 };

    // Shape
    float aspect    = 0.0f;    // -100..100, stretches x vs y
    float round     = 0.0f;    // 0..100, corner rounding
    float width     = 10.0f;   // 0..100, stroke / line thickness
    float length    = 80.0f;   // 0..100, line length, wave depth
    int   step      = 12;      // repeat count for RadiateLine / HLines / VLines / WaveCircle

    // LED. Colour is the lens; the hot core is derived from it rather than
    // picked separately, because every LED blows out toward white at the die
    // whatever colour its lens is.
    LedShape ledShape  = LedShape::Circle;
    bool  ledOn        = true;

    /** Brightness ramps with the frame instead of being fixed, so a two-frame
        strip gives off-then-on and a sixty-five frame strip gives a meter. */
    bool  ledFollowFrame = false;

    /** The lens as a fraction of the layer, so the bloom has somewhere to go.
        A lens filling its own layer leaves the halo nowhere to fade out and the
        image's rectangular edge shows through it. */
    float ledSize      = 70.0f;   // 5..100 %

    float ledIntensity = 100.0f;  // 0..100, drive level when lit
    float ledCore      = 35.0f;   // 0..100, size of the blown-out centre
    float ledOffLevel  = 22.0f;   // 0..100, how lit the lens looks when dark
    float ledGlow      = 45.0f;   // 0..100, bloom spilling past the lens
    float ledBezel     = 14.0f;   // 0..100, rim thickness

    juce::Colour ledBezelColour { 0xff202024 };

    // Meter scales — the tick ring around a Moog knob, and the same idea
    // stacked alongside a slider. Count comes from Step, the long tick's length
    // from Length, its thickness from Width and its end rounding from Round.
    // Ticks straddle a ring: Length reaches inward from it toward the knob and
    // meterOuterLen reaches outward toward the edge. The ring is placed so the
    // outer ends always land on the layer's boundary, so no amount of outward
    // reach can push the scale off the canvas.
    float meterOuterLen   = 0.0f;     // 0..100 %, outward reach
    int   meterEvery      = 5;        // 1..32, a long tick every N
    float meterMinorLen   = 45.0f;    // 0..100 %, short tick against the long one
    float meterMinorWidth = 65.0f;    // 0..100 %, short thickness against the long one
    float meterStart      = -135.0f;  // degrees, MeterCircle arc start
    float meterEnd        =  135.0f;  // degrees, MeterCircle arc end

    /** Values printed against the long ticks, left to right, comma separated.
        A lone "@" holds that position blank — which is how you label only the
        ends of a scale, or skip the centre, without the rest shifting along. */
    juce::String meterLabels;
    float meterLabelGap    = 10.0f;   // 0..100 %, clearance from the tick's end
    bool  meterLabelsInside = false;  // print them toward the middle instead

    // RadiateLine shaping. Bend sweeps each spoke round instead of leaving it
    // radial, which is what turns a plain star into a shuriken; skew tapers the
    // far end against the near one for a trapeze; roundness rounds the corners.
    float rayBend      = 0.0f;    // -90..90 degrees
    float raySkew      = 0.0f;    // -100..100 %, outer width against inner
    float rayRoundness = 0.0f;    // 0..100 %
    float angleStep = 0.0f;    // -180..180, rotation offset per repeat

    // Rim shading for the analytic circle family only — CircleFill, MetalCircle
    // and Sphere build their own bevel from these inside renderCircleFamily.
    // Every other primitive gets its bevel from BevelSpec instead.
    float emboss        = 0.0f;    // -100..100, rim strength; negative carves inward
    float embossDiffuse = 60.0f;   // 0..100
    float ambient       = 35.0f;   // 0..100
    float lightDir      = 135.0f;  // degrees, 135 = upper-left
    float specular      = 40.0f;   // 0..100
    float specularWidth = 25.0f;   // 1..100, higher = tighter highlight
    float diffuse       =  0.0f;   // 0..100 — edge fade, not a lighting term

    /** Our own gloss-contour lighting, 0..100. A multi-stop curve along the
        light axis with a deliberate non-monotonic lift near the dark end —
        the bounce off the panel below that stops plastic reading as matte.
        Emboss only bevels edges; this shades the flat interior. */
    float gloss         = 0.0f;

    // Text
    juce::String text  { "Text" };
    float fontSize     = 24.0f;
    bool  bold         = false;
    bool  italic       = false;
    int   textAlign    = 0;        // 0 centre, 1 left, 2 right

    BevelSpec  bevel;

    // enabled, colour, opacity, offsetX, offsetY, size, fade, knockout
    ShadowSpec shadow;
    // ... enabled, colour, opacity, offsetX, offsetY, size, fade, knockout,
    //     curve, hotCore, blend
    ShadowSpec innerShadow { false, juce::Colour (0xff000000), 55.0f, 4.0f, 4.0f,  8.0f, 70.0f, false,  0.0f,  0.0f, BlendMode::Normal };
    ShadowSpec outerGlow   { false, juce::Colour (0xffffd060), 75.0f, 0.0f, 0.0f, 12.0f, 85.0f, true,  70.0f, 55.0f, BlendMode::Screen };

    // Outline. Width is in canvas pixels at 1x — the renderer scales it with
    // everything else, so a stroke stays the same relative weight whether the
    // preview is drawn at 1x or 4x.
    bool           strokeEnabled  = false;
    juce::Colour   strokeColour   { 0xff101014 };
    float          strokeWidth    = 2.0f;    // 0.25..40
    StrokePosition strokePosition = StrokePosition::Outside;

    // Texture. Bump leaves the colour alone and only disturbs the lighting,
    // which is what KnobMan does. Fill instead lets the pattern through, which
    // is what a wood or carbon PNG actually wants.
    TextureMode  textureMode  = TextureMode::Off;
    juce::String textureName  { "Hairline" };
    float        textureDepth = 50.0f;   // 0..100
    float        textureScale = 25.0f;   // 2..200, % of the canvas per repeat
    float        textureAngle = 0.0f;    // -180..180

    // Image
    juce::String imageFile;

    // Shape: the name of an entry in the shape inventory. Referenced rather
    // than copied, so editing the shape in the Shapes tab updates every layer
    // using it — the point of having one house outline across a portfolio.
    juce::String shapeName;

    // Kept as a fallback for documents made before the inventory existed, and
    // for imported .knob files whose shapes are not in it.
    std::vector<juce::Point<float>> shapePoints;

    bool fill = true;
};

//==============================================================================
enum class MaskType { Rotate = 0, Radius, Horizontal, Vertical, numTypes };

juce::StringArray maskTypeNames();

struct MaskSpec
{
    bool     enabled = false;
    MaskType type    = MaskType::Rotate;
    bool     orOp    = false;   // false = And with what came before
    bool     biDir   = false;   // mirror the kept range
    AnimVal  start   { -180.0f };
    AnimVal  stop    {  180.0f };
};

//==============================================================================
struct EffectSpec
{
    bool antialias = true;

    bool    zoomSeparate = false;
    AnimVal zoomX { 100.0f };
    AnimVal zoomY { 100.0f };

    AnimVal offsetX { 0.0f };
    AnimVal offsetY { 0.0f };

    AnimVal angle { 0.0f };
    bool    keepDir = false;    // counter-rotate so content stays upright

    float centreX = 50.0f;      // rotation pivot, % of canvas
    float centreY = 50.0f;

    AnimVal alpha { 100.0f };

    AnimVal brightness { 0.0f };   // -100..100
    AnimVal contrast   { 0.0f };
    AnimVal saturation { 0.0f };
    AnimVal hue        { 0.0f };   // -180..180

    MaskSpec mask1;
    MaskSpec mask2;
};

//==============================================================================
struct DocLayer
{
    juce::String  name { "Layer" };
    bool          visible = true;
    bool          solo    = false;
    PrimitiveSpec prim;
    EffectSpec    eff;
};

//==============================================================================
struct LayerDoc
{
    int canvasWidth  = 128;
    int canvasHeight = 128;
    int frames       = 65;

    // Global Light. A bevel with useGlobalLight set reads these instead of its
    // own angle, so every layer on one knob is lit from the same place.
    float globalLightAngle    = 120.0f;
    float globalLightAltitude =  30.0f;

    std::vector<DocLayer> layers;

    //--------------------------------------------------------------------------
    /** True if any layer is soloed; render then shows only soloed layers. */
    bool hasSolo() const noexcept;

    void addLayer (const DocLayer&);
    void removeLayer (int index);
    void moveLayer (int from, int to);

    //--------------------------------------------------------------------------
    static LayerDoc emptyDocument();

    /** A fluted knob assembled from primitives — the worked example. */
    static LayerDoc knobExample();

    /** A vertical slider cap, to show the same engine making a different widget. */
    static LayerDoc sliderExample();

    static juce::StringArray exampleNames();
    static LayerDoc          exampleByIndex (int index);

    //--------------------------------------------------------------------------
    juce::ValueTree toValueTree() const;
    static LayerDoc fromValueTree (const juce::ValueTree&);
};

//==============================================================================
/** Stella: where an Image layer's picture lives.

    KnobMaker keeps the full path of the file that was chosen. A Stella look belongs to a
    project, so a chosen picture is copied into the project's pictures folder and the
    layer keeps just its name: the project opens the same on any computer. A full path
    still works, as in KnobMaker. */
namespace LayerImages
{
    /** The open project's pictures folder (gui/images); none: no project. */
    void setFolder (const juce::File& folder);
    juce::File getFolder();

    /** A full path as it is; a bare name in the pictures folder. */
    juce::File resolve (const juce::String& imageFile);

    /** A picture the user chose for a layer, copied into the pictures folder (when there
        is one): what the layer keeps. Set by the studio. Without it: the full path. */
    void setAdopter (std::function<juce::String (const juce::File&)> adopter);
    juce::String adopt (const juce::File& chosen);
}
