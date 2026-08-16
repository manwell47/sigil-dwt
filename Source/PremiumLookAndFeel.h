#pragma once

#include <JuceHeader.h>

class PremiumLookAndFeel : public juce::LookAndFeel_V4
{
public:
    PremiumLookAndFeel();
    
    void drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height,
                           float sliderPosProportional, float rotaryStartAngle,
                           float rotaryEndAngle, juce::Slider& slider) override;
                           
    void drawLabel (juce::Graphics& g, juce::Label& label) override;
};
