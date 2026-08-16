#include "PremiumLookAndFeel.h"

PremiumLookAndFeel::PremiumLookAndFeel()
{
    // Minimalistic styling
}

void PremiumLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height,
                                           float sliderPos, float rotaryStartAngle, float rotaryEndAngle, juce::Slider& slider)
{
    auto radius = (float) juce::jmin (width / 2, height / 2) - 4.0f;
    auto centreX = (float) x + (float) width  * 0.5f;
    auto centreY = (float) y + (float) height * 0.5f;
    auto rx = centreX - radius;
    auto ry = centreY - radius;
    auto rw = radius * 2.0f;
    auto angle = rotaryStartAngle + sliderPos * (rotaryEndAngle - rotaryStartAngle);
    
    // Track Background (Dark Grey)
    g.setColour (juce::Colour (0xff202020));
    g.fillEllipse (rx, ry, rw, rw);
    
    // Track Arc (Unfilled portion)
    juce::Path backgroundArc;
    backgroundArc.addCentredArc (centreX, centreY, radius - 2.0f, radius - 2.0f, 0.0f, rotaryStartAngle, rotaryEndAngle, true);
    g.setColour (juce::Colour (0xff333333));
    g.strokePath (backgroundArc, juce::PathStrokeType (4.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    
    // Fill Arc (Value portion)
    if (slider.isEnabled())
    {
        juce::Path valueArc;
        valueArc.addCentredArc (centreX, centreY, radius - 2.0f, radius - 2.0f, 0.0f, rotaryStartAngle, angle, true);
        
        // Use a nice premium gradient based on the slider's colour ID
        juce::Colour baseColor = slider.findColour (juce::Slider::rotarySliderFillColourId);
        juce::ColourGradient grad (baseColor.brighter(0.2f), centreX, ry, baseColor.darker(0.2f), centreX, ry + rw, false);
        g.setGradientFill (grad);
        
        g.strokePath (valueArc, juce::PathStrokeType (4.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
    
    // Knob Thumb/Indicator
    juce::Path p;
    auto pointerLength = radius * 0.33f;
    auto pointerThickness = 2.5f;
    p.addRectangle (-pointerThickness * 0.5f, -radius, pointerThickness, pointerLength);
    p.applyTransform (juce::AffineTransform::rotation (angle).translated (centreX, centreY));
    
    g.setColour (juce::Colours::white);
    g.fillPath (p);
}

void PremiumLookAndFeel::drawLabel (juce::Graphics& g, juce::Label& label)
{
    g.fillAll (label.findColour (juce::Label::backgroundColourId));
    
    if (!label.isBeingEdited())
    {
        auto alpha = label.isEnabled() ? 1.0f : 0.5f;
        const juce::Font font ("Inter", 13.0f, juce::Font::plain); // Modern sans-serif
        
        g.setColour (label.findColour (juce::Label::textColourId).withMultipliedAlpha (alpha));
        g.setFont (font);
        
        auto textArea = getLabelBorderSize(label).subtractedFrom (label.getLocalBounds());
        
        g.drawFittedText (label.getText(), textArea, label.getJustificationType(),
                          juce::jmax (1, (int) ((float) textArea.getHeight() / font.getHeight())),
                          label.getMinimumHorizontalScale());
        
        // Draw bottom subtle border for text boxes
        if (label.findColour(juce::Label::outlineColourId).isOpaque())
        {
            g.setColour(juce::Colours::grey.withAlpha(0.2f));
            g.drawLine((float)textArea.getX(), (float)label.getHeight() - 1.0f, (float)textArea.getRight(), (float)label.getHeight() - 1.0f, 1.0f);
        }
    }
    else if (label.isEnabled())
    {
        g.setColour (label.findColour (juce::Label::outlineColourId));
        g.drawRect (label.getLocalBounds());
    }
}
