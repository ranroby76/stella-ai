// C:\workspace\Stella AI Studio\src\Looks.cpp

#include "Looks.h"
#include "LayerRender.h"

#include "StellaLooks.h"

#include <algorithm>

namespace
{
    /** Unique across every look ever read or changed, so a drawing is never mistaken for
        another look's. */
    int nextRevision()
    {
        static int revision = 0;
        return ++revision;
    }

    /** A look's kind when its file doesn't say: from its shape and frame count. */
    Looks::Kind guessKind (const LayerDoc& doc)
    {
        if (doc.frames == 2)                                return Looks::Kind::button;
        if (doc.canvasWidth >= doc.canvasHeight * 2)        return Looks::Kind::sliderAcross;
        if (doc.canvasHeight >= doc.canvasWidth * 2)        return Looks::Kind::slider;
        return Looks::Kind::knob;
    }

    /** The words of a name a look might be known by, with a few that mean the same. The
        old KnobMaker style names (cream, black, metal) land on the matching looks. */
    juce::StringArray wordsOf (const juce::String& name)
    {
        auto words = juce::StringArray::fromTokens (name.toLowerCase(), " -_.,", "");
        words.removeEmptyStrings();

        for (int i = words.size(); --i >= 0;)
        {
            const auto w = words[i];

            if (w == "metal" || w == "aluminium" || w == "aluminum" || w == "chrome" || w == "steel")  words.add ("silver");
            if (w == "brass" || w == "golden")                                                     words.add ("gold");
            if (w == "ivory" || w == "white" || w == "vintage" || w == "retro")                    words.add ("cream");
            if (w == "chicken" || w == "chickenhead" || w == "davies")                             words.add ("pointer");
            if (w == "arc" || w == "modern" || w == "flat" || w == "minimal")                      words.add ("ring");
            if (w == "led" || w == "lit" || w == "illuminated" || w == "glow")                     words.add ("light");
            if (w == "lever" || w == "bat")                                                        words.add ("toggle");
            if (w == "numbers" || w == "numbered" || w == "dial")                                  words.add ("scale");
        }

        // Words every look of a kind has say nothing about which one.
        for (const auto* generic : { "knob", "knobs", "slider", "sliders", "fader", "faders", "switch", "switches", "button", "buttons", "look", "style" })
            words.removeString (generic);

        return words;
    }

    /** A look that's always there, for when the built-in ones are missing. */
    Looks::Look fallbackLook (Looks::Kind kind)
    {
        Looks::Look look;
        look.kind = kind;
        look.revision = nextRevision();

        switch (kind)
        {
            case Looks::Kind::knob:
                look.name = "Knob";
                look.doc = LayerDoc::knobExample();
                break;

            case Looks::Kind::slider:
            case Looks::Kind::sliderAcross:
            {
                const bool across = kind == Looks::Kind::sliderAcross;
                look.name = across ? "Slider across" : "Slider";
                look.doc = LayerDoc::sliderExample();   // its cap travels from the top down

                for (auto& layer : look.doc.layers)
                {
                    const auto travel = layer.eff.offsetY;

                    if (across)
                    {
                        // Turned on its side: left to right.
                        layer.eff.offsetY = AnimVal (0.0f);
                        layer.eff.offsetX = AnimVal (travel.from, travel.to, travel.mode);
                        std::swap (layer.eff.zoomX, layer.eff.zoomY);
                    }
                    else
                    {
                        // Up from the bottom, as a slider moves.
                        layer.eff.offsetY = AnimVal (travel.to, travel.from, travel.mode);
                    }
                }

                if (across)
                    std::swap (look.doc.canvasWidth, look.doc.canvasHeight);
                break;
            }

            case Looks::Kind::button:
            {
                look.name = "Switch";
                look.doc.canvasWidth = look.doc.canvasHeight = 64;
                look.doc.frames = 2;
                look.doc.layers.clear();

                DocLayer cap;
                cap.name = "Cap";
                cap.prim.type = PrimType::RectFill;
                cap.prim.colour = juce::Colour (0xff36363c);
                cap.prim.round = 30.0f;
                cap.eff.zoomX = cap.eff.zoomY = AnimVal (70.0f);
                look.doc.addLayer (cap);

                DocLayer light;
                light.name = "Light";
                light.prim.type = PrimType::Led;
                light.prim.colour = juce::Colour (0xffffa21a);
                light.prim.ledFollowFrame = true;
                light.eff.zoomX = light.eff.zoomY = AnimVal (50.0f);
                look.doc.addLayer (light);
                break;
            }
        }

        return look;
    }

    juce::Image renderFitted (const LayerDoc& doc, float t, int width, int height)
    {
        juce::Image out (juce::Image::ARGB, juce::jmax (1, width), juce::jmax (1, height), true);

        const auto docW = (float) juce::jmax (8, doc.canvasWidth), docH = (float) juce::jmax (8, doc.canvasHeight);
        const auto fit = juce::jmin ((float) out.getWidth() / docW, (float) out.getHeight() / docH);

        // Drawn at the size it shows, not shrunk from a big one: the layers are described in
        // fractions of the canvas, so this is the sharpest it can be.
        const auto rendered = LayerRender::renderFrame (doc, t, fit);

        juce::Graphics g (out);
        g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
        g.drawImage (rendered, juce::Rectangle<float> (docW * fit, docH * fit)
                                   .withCentre ({ (float) out.getWidth() * 0.5f, (float) out.getHeight() * 0.5f }));
        return out;
    }
}

//==============================================================================
juce::String Looks::kindName (Kind kind)
{
    switch (kind)
    {
        case Kind::knob:          return "knob";
        case Kind::slider:        return "slider";
        case Kind::sliderAcross:  return "hslider";
        case Kind::button:        return "switch";
    }

    return "knob";
}

Looks::Kind Looks::kindFromName (const juce::String& name)
{
    const auto n = name.trim().toLowerCase();

    if (n == "switch" || n == "button" || n == "toggle")                         return Kind::button;
    if (n == "hslider" || n == "slider across" || n == "horizontal slider")     return Kind::sliderAcross;
    if (n == "slider" || n == "fader" || n == "vslider")                         return Kind::slider;
    return Kind::knob;
}

juce::String Looks::kindDisplayName (Kind kind)
{
    switch (kind)
    {
        case Kind::knob:          return "knob";
        case Kind::slider:        return "slider";
        case Kind::sliderAcross:  return "slider across";
        case Kind::button:        return "switch";
    }

    return "knob";
}

bool Looks::takesLook (const GuiWidget& widget)
{
    return widget.type == GuiWidget::Type::knob || widget.type == GuiWidget::Type::slider || widget.type == GuiWidget::Type::toggle;
}

Looks::Kind Looks::kindOf (const GuiWidget& widget)
{
    if (widget.type == GuiWidget::Type::slider)   return widget.vertical ? Kind::slider : Kind::sliderAcross;
    if (widget.type == GuiWidget::Type::toggle)   return Kind::button;
    return Kind::knob;
}

juce::File Looks::myLooksFolder()
{
    return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
               .getChildFile ("Stella AI Studio")
               .getChildFile ("Looks");
}

//==============================================================================
Looks::Looks()
{
    for (const auto kind : { Kind::knob, Kind::slider, Kind::sliderAcross, Kind::button })
        fallbacks[(int) kind] = fallbackLook (kind);

    // The built-in looks, compiled into the studio from the looks folder.
    for (int i = 0; i < StellaLooks::namedResourceListSize; ++i)
    {
        const auto* resource = StellaLooks::namedResourceList[i];
        int size = 0;

        if (const auto* data = StellaLooks::getNamedResource (resource, size); data != nullptr && size > 0)
        {
            // "Black knob.fklayers" -> "Black knob" (just the name: it's not a path on this computer).
            const auto file = juce::String (StellaLooks::getNamedResourceOriginalFilename (resource))
                                  .replaceCharacter ('\\', '/').fromLastOccurrenceOf ("/", false, false);
            Look look;

            if (readXml (juce::String::fromUTF8 (data, size), file.upToLastOccurrenceOf (".", false, false), look))
            {
                look.origin = Origin::builtIn;
                builtIn[look.name] = look;
            }
        }
    }

    readFolder (myLooksFolder(), Origin::mine, mine);
}

//==============================================================================
bool Looks::readXml (const juce::String& xml, const juce::String& name, Look& look)
{
    const auto parsed = juce::parseXML (xml);

    if (parsed == nullptr)
        return false;

    const auto tree = juce::ValueTree::fromXml (*parsed);

    if (! tree.isValid() || ! tree.hasType ("LayerDoc") || name.trim().isEmpty())
        return false;

    look.name = name.trim();
    look.doc = LayerDoc::fromValueTree (tree);
    look.kind = tree.hasProperty ("stellaKind") ? kindFromName (tree["stellaKind"].toString()) : guessKind (look.doc);
    look.order = (int) tree.getProperty ("stellaOrder", 1000);
    look.description = tree.getProperty ("stellaDescription", {}).toString();
    look.revision = nextRevision();
    return true;
}

bool Looks::readFile (const juce::File& file, Look& look)
{
    return file.existsAsFile() && readXml (file.loadFileAsString(), file.getFileNameWithoutExtension(), look);
}

bool Looks::writeFile (const juce::File& file, const Look& look)
{
    auto tree = look.doc.toValueTree();
    tree.setProperty ("stellaKind", kindName (look.kind), nullptr);

    if (look.order != 1000)
        tree.setProperty ("stellaOrder", look.order, nullptr);

    if (look.description.isNotEmpty())
        tree.setProperty ("stellaDescription", look.description, nullptr);

    const auto xml = tree.createXml();

    if (xml == nullptr || ! file.getParentDirectory().createDirectory())
        return false;

    return xml->writeTo (file, {});
}

void Looks::readFolder (const juce::File& folder, Origin origin, std::map<juce::String, Look>& into)
{
    if (! folder.isDirectory())
        return;

    for (const auto& file : folder.findChildFiles (juce::File::findFiles, false, juce::String ("*") + fileExtension))
    {
        Look look;

        if (readFile (file, look))
        {
            look.origin = origin;
            into[look.name] = look;
        }
    }
}

//==============================================================================
juce::File Looks::projectFolder() const
{
    return guiFolder != juce::File() ? guiFolder.getChildFile (folderName) : juce::File();
}

void Looks::setGuiFolder (const juce::File& folder)
{
    guiFolder = folder;
    reload();
}

void Looks::reload()
{
    project.clear();

    if (guiFolder != juce::File())
        readFolder (projectFolder(), Origin::project, project);

    mine.clear();
    readFolder (myLooksFolder(), Origin::mine, mine);

    frames.clear();
    framesDrawn = 0;
    changed();
}

void Looks::changed()
{
    if (onChanged != nullptr)
        onChanged();
}

//==============================================================================
const Looks::Look* Looks::findIn (const std::map<juce::String, Look>& looks, const juce::String& name) const
{
    if (const auto found = looks.find (name); found != looks.end())
        return &found->second;

    for (const auto& [key, look] : looks)
        if (key.equalsIgnoreCase (name.trim()))
            return &look;

    return nullptr;
}

const Looks::Look* Looks::find (const juce::String& name) const
{
    if (name.trim().isEmpty())
        return nullptr;

    if (const auto* found = findIn (project, name))   return found;
    if (const auto* found = findIn (mine, name))      return found;
    return findIn (builtIn, name);
}

const Looks::Look* Looks::find (const juce::String& name, Origin origin) const
{
    if (name.trim().isEmpty())
        return nullptr;

    return findIn (origin == Origin::project ? project : origin == Origin::mine ? mine : builtIn, name);
}

const Looks::Look& Looks::defaultFor (Kind kind) const
{
    const Look* best = nullptr;

    for (const auto& [name, look] : builtIn)
        if (look.kind == kind && (best == nullptr || look.order < best->order))
            best = &look;

    if (best == nullptr)
        return fallbacks.at ((int) kind);

    // The plugin's own version of it, once it's been changed here.
    if (const auto* own = find (best->name); own != nullptr && own->kind == kind)
        return *own;

    return *best;
}

const Looks::Look& Looks::lookFor (const GuiWidget& widget) const
{
    const auto kind = kindOf (widget);

    if (widget.style.isEmpty())
        return defaultFor (kind);

    if (const auto* found = find (widget.style); found != nullptr && found->kind == kind)
        return *found;

    // A loose name: the look of this kind that shares the most words with it.
    const auto words = wordsOf (widget.style);
    const Look* best = nullptr;
    int bestScore = 0;

    for (const auto* look : listFor (kind))
    {
        int score = 0;

        for (const auto& w : words)
            if (look->name.containsIgnoreCase (w))
                ++score;

        if (score > bestScore)
        {
            best = look;
            bestScore = score;
        }
    }

    return best != nullptr ? *best : defaultFor (kind);
}

std::vector<const Looks::Look*> Looks::listFor (Kind kind) const
{
    std::vector<const Look*> list;
    juce::StringArray names;

    auto addFrom = [&] (const std::map<juce::String, Look>& looks)
    {
        std::vector<const Look*> some;

        for (const auto& [name, look] : looks)
            if (look.kind == kind && ! names.contains (name, true))
                some.push_back (&look);

        std::stable_sort (some.begin(), some.end(), [] (const Look* a, const Look* b) { return a->order < b->order; });

        for (const auto* look : some)
        {
            list.push_back (look);
            names.add (look->name);
        }
    };

    addFrom (project);
    addFrom (mine);
    addFrom (builtIn);
    return list;
}

std::vector<const Looks::Look*> Looks::listFrom (Origin origin, Kind kind) const
{
    std::vector<const Look*> list;

    for (const auto& [name, look] : origin == Origin::project ? project : origin == Origin::mine ? mine : builtIn)
        if (look.kind == kind)
            list.push_back (&look);

    std::stable_sort (list.begin(), list.end(), [] (const Look* a, const Look* b) { return a->order < b->order; });
    return list;
}

std::vector<const Looks::Look*> Looks::all() const
{
    std::vector<const Look*> list;

    for (const auto kind : { Kind::knob, Kind::slider, Kind::sliderAcross, Kind::button })
        for (const auto* look : listFor (kind))
            list.push_back (look);

    return list;
}

//==============================================================================
void Looks::setProjectLook (const juce::String& name, Kind kind, const LayerDoc& doc, const juce::String& description)
{
    if (name.trim().isEmpty())
        return;

    Look look;

    // A look set aside keeps what it said about itself.
    if (const auto* before = find (name))
    {
        look.description = before->description;
        look.order = before->order;
    }

    if (description.isNotEmpty())
        look.description = description;

    look.name = name.trim();
    look.kind = kind;
    look.origin = Origin::project;
    look.doc = doc;
    look.revision = nextRevision();

    // The drawings of what it was are no use now.
    if (const auto before = project.find (look.name); before != project.end())
        frames.erase (before->second.revision);

    if (guiFolder != juce::File())
        writeFile (projectFolder().getChildFile (juce::File::createLegalFileName (look.name) + fileExtension), look);

    project[look.name] = look;
    changed();
}

bool Looks::removeProjectLook (const juce::String& name)
{
    const auto found = project.find (name);

    if (found == project.end())
        return false;

    if (guiFolder != juce::File())
        projectFolder().getChildFile (juce::File::createLegalFileName (found->second.name) + fileExtension).deleteFile();

    frames.erase (found->second.revision);
    project.erase (found);
    changed();
    return true;
}

juce::String Looks::freeName (const juce::String& base, const juce::StringArray& alsoTaken) const
{
    auto taken = [&] (const juce::String& name)
    {
        return findIn (project, name) != nullptr || alsoTaken.contains (name, true);
    };

    auto stem = base.trim().isNotEmpty() ? base.trim() : juce::String ("Look");

    if (! taken (stem))
        return stem;

    // "Black knob 2" grows to "Black knob 3", not "Black knob 2 2".
    if (const auto last = stem.fromLastOccurrenceOf (" ", false, false); last.containsOnly ("0123456789") && last.isNotEmpty())
        stem = stem.upToLastOccurrenceOf (" ", false, false);

    for (int i = 2;; ++i)
        if (const auto candidate = stem + " " + juce::String (i); ! taken (candidate))
            return candidate;
}

bool Looks::saveToMine (const juce::String& name)
{
    const auto* look = find (name);

    if (look == nullptr)
        return false;

    auto copy = *look;
    copy.origin = Origin::mine;
    copy.revision = nextRevision();

    if (! writeFile (myLooksFolder().getChildFile (juce::File::createLegalFileName (copy.name) + fileExtension), copy))
        return false;

    mine[copy.name] = copy;
    changed();
    return true;
}

bool Looks::adoptMine (const GuiLayout& layout)
{
    if (guiFolder == juce::File())
        return false;

    bool adopted = false;

    for (const auto& w : layout.widgets)
    {
        if (! takesLook (w) || w.style.isEmpty() || findIn (project, w.style) != nullptr)
            continue;

        if (const auto* own = findIn (mine, w.style); own != nullptr && own->kind == kindOf (w))
        {
            auto copy = *own;
            copy.origin = Origin::project;
            copy.revision = nextRevision();

            if (writeFile (projectFolder().getChildFile (juce::File::createLegalFileName (copy.name) + fileExtension), copy))
            {
                project[copy.name] = copy;
                adopted = true;
            }
        }
    }

    if (adopted)
        changed();

    return adopted;
}

//==============================================================================
int Looks::framesFor (const Look& look, Kind kind)
{
    return kind == Kind::button ? 2 : juce::jlimit (1, 512, look.doc.frames);
}

juce::Image Looks::frame (const Look& look, Kind kind, int width, int height, float value, float resolution)
{
    width = juce::jmax (1, width);
    height = juce::jmax (1, height);

    const auto count = framesFor (look, kind);
    const auto index = count > 1 ? juce::jlimit (0, count - 1, juce::roundToInt (juce::jlimit (0.0f, 1.0f, value) * (float) (count - 1))) : 0;
    const auto res = juce::jlimit (1.0f, 3.0f, resolution);

    const auto key = juce::String (width) + "x" + juce::String (height) + "|" + juce::String (index) + "/" + juce::String (count)
                   + "@" + juce::String (juce::roundToInt (res * 4.0f));

    auto& drawn = frames[look.revision];

    if (const auto found = drawn.find (key); found != drawn.end())
        return found->second;

    // Plenty for every control of a big plugin at two zooms; past that, start again.
    if (++framesDrawn > 6000)
    {
        for (auto& [revision, images] : frames)
            images.clear();

        framesDrawn = 1;
    }

    const auto t = count > 1 ? (float) index / (float) (count - 1) : 0.0f;
    auto image = renderFitted (look.doc, t, juce::roundToInt ((float) width * res), juce::roundToInt ((float) height * res));
    drawn[key] = image;
    return image;
}

juce::Image Looks::strip (const Look& look, Kind kind, int width, int height)
{
    const auto count = framesFor (look, kind);
    juce::Image image (juce::Image::ARGB, juce::jmax (1, width), juce::jmax (1, height) * count, true);
    juce::Graphics g (image);

    for (int i = 0; i < count; ++i)
        g.drawImageAt (frame (look, kind, width, height, count > 1 ? (float) i / (float) (count - 1) : 0.0f), 0, i * height);

    return image;
}
