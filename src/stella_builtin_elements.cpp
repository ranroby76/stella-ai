// C:\workspace\Stella AI Studio\src\stella_builtin_elements.cpp
//
// The toolbox's own elements that are made like programmed ones (stella_element_api.h),
// compiled with the elements' runtime into the studio's module and into every exported
// plugin that uses them. A project's own element of the same name takes a built-in's place.
//
//   DropDown   a drop-down list: "type": "dropdown" in a layout

#include "stella_elements.h"

#include <algorithm>
#include <cmath>

namespace stella::ui::builtin
{
    //==========================================================================
    /** A drop-down list for a whole-number parameter (0..N-1): the chosen option in a box,
        and a click opens the list under it. A long list opens in columns, so it never runs
        off the window: "settings": { "rows": 12 } is the most in one column. */
    class DropDown : public Element
    {
    public:
        void paint (Canvas& g) override
        {
            const auto accent = colour (0xffe5484d);
            const auto top = captionHeight();

            if (top > 0.0f)
                g.drawText (label(), 0.0f, 0.0f, (float) width(), top, 12.5f, 0xffd8d4ca, Align::left);

            const float x = 0.5f, y = top + 0.5f, w = (float) width() - 1.0f, h = (float) height() - top - 1.0f;
            const auto open = isPopupOpen();

            g.fillGradient (x, y, w, h, hovering || open ? 0xff34343c : 0xff26262c, hovering || open ? 0xff2a2a31 : 0xff1c1c21, 4.0f);
            g.drawRoundedRect (x, y, w, h, 4.0f, 1.0f, open ? accent : (hovering ? 0x50ffffff : 0x34ffffff));
            g.drawText (optionText (current()), x + 10.0f, y, w - 34.0f, h, std::min (13.5f, std::max (9.0f, h * 0.45f)), 0xffeeeeee, Align::left);

            // The arrow: down, or up while the list is open.
            const float ax = x + w - 15.0f, ay = y + h * 0.5f, s = std::min (5.0f, h * 0.18f);

            if (open) g.fillTriangle (ax - s, ay + s * 0.5f, ax + s, ay + s * 0.5f, ax, ay - s * 0.7f, accent);
            else      g.fillTriangle (ax - s, ay - s * 0.5f, ax + s, ay - s * 0.5f, ax, ay + s * 0.7f, accent);
        }

        bool mouseDown (const Mouse&) override
        {
            hoverIndex = current();
            const auto shape = layout();
            openPopup (0.0f, (float) height() + 2.0f, (float) shape.columns * shape.columnWidth + 8.0f, (float) shape.rows * rowHeight + 8.0f);
            return false;
        }

        void mouseMove (const Mouse&) override    { hovering = true; }
        void mouseExit() override                 { hovering = false; }

        void mouseWheel (const Mouse&, float notches) override
        {
            if (notches != 0.0f)
                choose (std::clamp (current() + (notches > 0.0f ? -1 : 1), 0, count() - 1));
        }

        void paintPopup (Canvas& g) override
        {
            const auto accent = colour (0xffe5484d);
            const float w = (float) g.width(), h = (float) g.height();
            const auto shape = layout();

            g.fillRoundedRect (0.0f, 0.0f, w, h, 5.0f, 0xf41e1e23);
            g.drawRoundedRect (0.0f, 0.0f, w, h, 5.0f, 1.0f, 0x40ffffff);

            for (int i = 0; i < count(); ++i)
            {
                const auto column = i / shape.rows, row = i % shape.rows;
                const float cellX = 4.0f + (float) column * shape.columnWidth, cellY = 4.0f + (float) row * rowHeight;
                const bool chosen = i == current();

                if (column > 0 && row == 0)
                    g.fillRect (cellX - 0.5f, 6.0f, 1.0f, h - 12.0f, 0x18ffffff);

                if (i == hoverIndex)
                    g.fillRoundedRect (cellX + 1.0f, cellY, shape.columnWidth - 2.0f, rowHeight, 3.0f, withAlpha (accent, 0.3f));

                if (chosen)
                    g.fillCircle (cellX + 9.0f, cellY + rowHeight * 0.5f, 3.0f, accent);

                g.drawText (optionText (i), cellX + 20.0f, cellY, shape.columnWidth - 24.0f, rowHeight, 13.0f,
                            chosen ? 0xffffffff : 0xffcfcfcf, Align::left, chosen);
            }
        }

        void popupMouseMove (const Mouse& m) override    { hoverIndex = indexAt (m.x, m.y); }
        void popupMouseExit() override                    { hoverIndex = -1; }

        void popupMouseWheel (const Mouse&, float notches) override
        {
            if (notches != 0.0f)
                hoverIndex = std::clamp ((hoverIndex < 0 ? current() : hoverIndex) + (notches > 0.0f ? -1 : 1), 0, count() - 1);
        }

        void popupMouseDown (const Mouse& m) override
        {
            if (const auto index = indexAt (m.x, m.y); index >= 0)
                choose (index);

            closePopup();
        }

    private:
        static constexpr float rowHeight = 24.0f;

        struct Shape
        {
            int columns, rows;
            float columnWidth;
        };

        bool hovering = false;
        int hoverIndex = -1;

        float captionHeight() const   { return ! label().empty() && height() >= 40 ? 18.0f : 0.0f; }
        int count() const             { return numOptions() > 0 ? numOptions() : std::max (1, choices()); }
        int current() const           { return std::clamp ((int) std::lround (value() - minimum()), 0, count() - 1); }

        std::string optionText (int i) const
        {
            return i < numOptions() ? option (i) : std::to_string (i + 1);
        }

        void choose (int index)
        {
            setValue (minimum() + (float) index);
        }

        /** The list's columns: as few as fit "rows" options each, as wide as the box. */
        Shape layout() const
        {
            const auto most = std::max (1, std::min (64, (int) std::lround (setting ("rows", 12.0f))));
            const auto columns = (count() + most - 1) / most;
            const auto rows = (count() + columns - 1) / columns;
            return { columns, rows, std::max ((float) width(), 120.0f) };
        }

        int indexAt (float x, float y) const
        {
            const auto shape = layout();
            const auto column = (int) std::floor ((x - 4.0f) / shape.columnWidth);
            const auto row = (int) std::floor ((y - 4.0f) / rowHeight);

            if (column < 0 || column >= shape.columns || row < 0 || row >= shape.rows)
                return -1;

            const auto index = column * shape.rows + row;
            return index < count() ? index : -1;
        }
    };

    STELLA_BUILTIN_ELEMENT (DropDown, "A drop-down list: the chosen option in a box; a click opens the list (long lists in columns). "
                                      "For a 0..N-1 parameter; \"options\" names the positions.")
}
