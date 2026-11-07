// Concert7 - open source true stereo convolution reverb
// Copyright (C) 2026 pschopen, GPL-3.0-or-later

#pragma once

#include "Widgets.h"
#include "../UserPresets.h"

namespace concert7
{
/**
    The preset browser: library presets and user presets in one list, with search, favourites, categories
    and tags. A click (or the arrow keys) loads a preset, so presets can be compared while browsing. User
    presets are saved, edited and deleted here; library presets can get tags and be favourites.
*/
class PresetBrowser : public juce::Component, private juce::ListBoxModel
{
public:
    explicit PresetBrowser (Concert7Processor& p);
    ~PresetBrowser() override;

    /** Reads the presets again (library, user preset folder, favourites, tags). */
    void refresh();
    /** Shows only one category ("" = all), e.g. when a category button was clicked. */
    void setCategory (const juce::String& category);
    /** The next / previous preset of the list as it is filtered (the < > buttons). */
    void step (int delta);
    /** Selects the preset in use and gives the search field the keyboard. */
    void opened();

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;

    std::function<void()> onClose;

    const std::vector<PresetEntry>& getShown() const { return shown; }
    void setSearchText (const juce::String& text);
    void setFilter (int f) { filter = f; applyFilter(); }   // 0 all, 1 favourites, 2 user presets
    void setTagSelected (const juce::String& tag, bool selected);

    enum { kAll = 0, kFavourites = 1, kUser = 2 };

private:
    class EditPanel;

    int getNumRows() override { return (int) shown.size(); }
    void paintListBoxItem (int row, juce::Graphics&, int width, int height, bool selected) override;
    void listBoxItemClicked (int row, const juce::MouseEvent&) override;
    void listBoxItemDoubleClicked (int row, const juce::MouseEvent&) override;
    void selectedRowsChanged (int lastRow) override;
    void returnKeyPressed (int row) override;
    juce::String getTooltipForRow (int row) override;

    void applyFilter();
    void rebuildFilterButtons();
    void choose (int row);
    juce::String currentKey() const;
    const PresetEntry* selectedEntry() const;
    void updateButtons();
    void showEditPanel (int mode);   // see EditPanel
    void save();
    void deleteSelected();

    Concert7Processor& processor;
    std::vector<PresetEntry> all, shown;
    juce::TextEditor search;
    juce::TextButton allButton { "ALL" }, favButton { "FAVOURITES" }, userButton { "MY PRESETS" };
    std::vector<std::unique_ptr<juce::TextButton>> categoryButtons, tagButtons;
    juce::StringArray categories, tags, selectedTags;
    juce::String category;
    IROrigin shownLibrary = IROrigin::Combined;   // the library whose presets are listed
    int filter = kAll;
    juce::ListBox list { "Presets", this };
    juce::TextButton saveButton { "SAVE" }, saveAsButton { "SAVE AS..." }, editButton { "EDIT..." },
                     deleteButton { "DELETE" }, folderButton { "FOLDER" }, closeButton { "CLOSE" };
    std::unique_ptr<EditPanel> editPanel;
    bool selectingCurrent = false;
    juce::String deleteArmedKey;   // DELETE asks once more
    juce::Rectangle<int> sideArea;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PresetBrowser)
};
} // namespace concert7
