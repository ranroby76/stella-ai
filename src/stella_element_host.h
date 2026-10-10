// C:\workspace\Stella AI Studio\src\stella_element_host.h
//
// What programmed GUI elements (stella_element_api.h) ask of whoever shows them: the
// studio, through WebAssembly, or an exported plugin's own GUI. element is the shower's
// own number for the element (its place in the layout). A slot is one of the element's
// parameters: 0 its own ("param"), 1 and up its roles ("params"), as findSlot gives them.

#pragma once

#include <cstdint>
#include <string>

namespace stella::ui
{
    class ElementHost
    {
    public:
        virtual ~ElementHost() = default;

        enum Info { valueInfo = 0, minInfo = 1, maxInfo = 2, defaultInfo = 3, skewInfo = 4 };

        /** A parameter's value or range; false if the slot has no parameter. */
        virtual bool paramInfo (int element, int slot, int info, float& result) = 0;
        virtual int findSlot (int element, const std::string& role) = 0;                  // -1: none
        virtual void setValue (int element, int slot, float newValue) = 0;
        virtual void gesture (int element, int slot, bool starts) = 0;                    // a drag starts or ends

        enum Text { labelText = 0, optionText = 1, paramNameText = 2, unitText = 3, settingText = 4 };

        /** option: index is which one; paramName and unit: index is the slot; setting: key
            names it. False if there's none. */
        virtual bool text (int element, int which, int index, const std::string& key, std::string& result) = 0;
        virtual int numOptions (int element) = 0;
        virtual std::uint32_t colour (int element) = 0;                                   // 0: none given

        virtual float level (int element, bool rms) = 0;
        virtual void readScope (int element, float* destination, int numSamples) = 0;
        virtual void playNote (int element, int note, float velocity) = 0;
        virtual bool isNoteDown (int note) = 0;
        virtual double seconds() = 0;
    };
}
