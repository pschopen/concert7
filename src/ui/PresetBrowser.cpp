// Concert7 - open source true stereo convolution reverb
// Copyright (C) 2026 pschopen, GPL-3.0-or-later

#include "PresetBrowser.h"

namespace concert7
{
using namespace juce;

namespace
{
    constexpr int kStarWidth = 30;

    void drawStar (Graphics& g, Rectangle<float> area, bool filled, Colour colour)
    {
        Path star;
        star.addStar (area.getCentre(), 5, area.getWidth() * 0.22f, area.getWidth() * 0.5f, 0.0f);
        g.setColour (colour);
        if (filled) g.fillPath (star);
        else g.strokePath (star, PathStrokeType (1.2f));
    }

    void styleButton (TextButton& b, float fontHeight = 10.5f)
    {
        b.getProperties().set ("fontHeight", fontHeight);
    }
}

//==============================================================================
/** Name, tags and notes of a preset: saving a new one, editing a user preset, tagging a library preset. */
class PresetBrowser::EditPanel : public Component
{
public:
    enum Mode { SaveAs, EditUser, TagLibrary };

    EditPanel (Mode m, const PresetEntry* entry, const String& suggestedName) : mode (m)
    {
        for (auto* ed : { &name, &tagsEditor, &notes })
        {
            ed->setFont (uiFont (14.0f));
            ed->setIndents (8, 6);
            addAndMakeVisible (ed);
        }
        notes.setMultiLine (true, true);
        notes.setReturnKeyStartsNewLine (true);
        name.setText (entry != nullptr && mode != SaveAs ? entry->name : suggestedName, false);
        tagsEditor.setText (entry != nullptr ? entry->tags.joinIntoString (", ") : String(), false);
        notes.setText (entry != nullptr && mode != TagLibrary ? entry->notes : String(), false);
        tagsEditor.setTextToShowWhenEmpty ("e.g. vocals, dark, drums", Colours::textFaint);
        notes.setTextToShowWhenEmpty ("Notes (optional)", Colours::textFaint);
        name.setEnabled (mode != TagLibrary);
        notes.setEnabled (mode != TagLibrary);
        name.onReturnKey = [this] { if (onOk) onOk(); };
        tagsEditor.onReturnKey = [this] { if (onOk) onOk(); };
        for (auto* ed : { &name, &tagsEditor, &notes })
            ed->onEscapeKey = [this] { if (onCancel) onCancel(); };

        okButton.setButtonText (mode == SaveAs ? "SAVE" : "APPLY");
        okButton.onClick = [this] { if (onOk) onOk(); };
        cancelButton.onClick = [this] { if (onCancel) onCancel(); };
        for (auto* b : { &okButton, &cancelButton })
        {
            styleButton (*b, 11.0f);
            addAndMakeVisible (b);
        }
    }

    void paint (Graphics& g) override
    {
        g.fillAll (Colours::background.withAlpha (0.75f));
        auto card = cardBounds().toFloat();
        g.setColour (Colours::panel);
        g.fillRoundedRectangle (card, 8.0f);
        g.setColour (Colours::outline);
        g.drawRoundedRectangle (card.reduced (0.5f), 8.0f, 1.0f);
        g.setColour (Colours::accent);
        g.setFont (labelFont (12.0f));
        const auto title = mode == SaveAs ? "SAVE PRESET" : mode == EditUser ? "EDIT PRESET" : "TAGS";
        g.drawText (title, card.reduced (20.0f, 14.0f).removeFromTop (16.0f), Justification::topLeft);
        g.setColour (Colours::textDim);
        g.setFont (labelFont (10.0f));
        for (auto [ed, label] : { std::pair<Component*, const char*> { &name, "NAME" }, { &tagsEditor, "TAGS (COMMA SEPARATED)" },
                                  { &notes, "NOTES" } })
            g.drawText (label, ed->getBounds().translated (0, -16).withHeight (14), Justification::bottomLeft);
        if (error.isNotEmpty())
        {
            g.setColour (Colours::warning);
            g.setFont (uiFont (13.0f));
            g.drawText (error, okButton.getBounds().withX (cardBounds().getX() + 20).withRight (cancelButton.getX() - 10),
                        Justification::centredLeft);
        }
    }

    void resized() override
    {
        auto r = cardBounds().reduced (20, 14);
        r.removeFromTop (34);
        name.setBounds (r.removeFromTop (30));
        r.removeFromTop (24);
        tagsEditor.setBounds (r.removeFromTop (30));
        r.removeFromTop (24);
        auto buttons = r.removeFromBottom (32);
        r.removeFromBottom (12);
        notes.setBounds (r);
        okButton.setBounds (buttons.removeFromRight (100));
        buttons.removeFromRight (8);
        cancelButton.setBounds (buttons.removeFromRight (90));
    }

    void focus() { (mode == TagLibrary ? tagsEditor : name).grabKeyboardFocus(); }
    void setError (const String& e) { error = e; repaint(); }

    Mode mode;
    TextEditor name, tagsEditor, notes;
    std::function<void()> onOk, onCancel;

private:
    Rectangle<int> cardBounds() const { return getLocalBounds().withSizeKeepingCentre (440, 330); }

    TextButton okButton, cancelButton { "CANCEL" };
    String error;
};

//==============================================================================
PresetBrowser::PresetBrowser (Concert7Processor& p) : processor (p)
{
    search.setFont (uiFont (14.0f));
    search.setIndents (10, 7);
    search.setTextToShowWhenEmpty ("Search name, category, tag, notes", Colours::textFaint);
    search.onTextChange = [this] { applyFilter(); };
    search.onEscapeKey = [this] {
        if (! search.isEmpty())
        {
            search.clear();
            applyFilter();
        }
        else if (onClose) onClose();
    };
    search.onReturnKey = [this] { if (! shown.empty()) list.grabKeyboardFocus(); };
    search.setTooltip ("Every word must appear in the name, category, tags or notes");
    addAndMakeVisible (search);

    list.setRowHeight (28);
    list.setColour (ListBox::backgroundColourId, Colours::lcdBack.withAlpha (0.55f));
    list.setColour (ListBox::outlineColourId, Colours::outline);
    list.setOutlineThickness (1);
    list.getVerticalScrollBar().setColour (ScrollBar::thumbColourId, Colours::accentDim);
    addAndMakeVisible (list);

    auto setFilterButton = [this] (TextButton& b, int f, const char* tip) {
        styleButton (b);
        b.getProperties().set ("led", true);
        b.setTooltip (tip);
        b.onClick = [this, f] { filter = f; applyFilter(); };
        addAndMakeVisible (b);
    };
    setFilterButton (allButton, kAll, "All presets");
    setFilterButton (favButton, kFavourites, "Only favourites (click the star of a preset)");
    setFilterButton (userButton, kUser, "Only your own presets");

    saveButton.setTooltip ("Saves the current sound into the user preset in use, or as a new one");
    saveAsButton.setTooltip ("Saves the current sound as a new user preset: impulse response and every setting");
    editButton.setTooltip ("Renames the selected user preset and edits its tags and notes (library presets: tags)");
    deleteButton.setTooltip ("Moves the selected user preset to the trash");
    folderButton.setTooltip ("Shows the folder with your presets (*.c7preset files can be copied between computers)");
    closeButton.setTooltip ("Back to the controls (Esc)");
    saveButton.onClick = [this] { save(); };
    saveAsButton.onClick = [this] { showEditPanel (EditPanel::SaveAs); };
    editButton.onClick = [this] {
        if (auto* e = selectedEntry()) showEditPanel (e->user ? EditPanel::EditUser : EditPanel::TagLibrary);
    };
    deleteButton.onClick = [this] { deleteSelected(); };
    folderButton.onClick = [this] {
        const auto folder = processor.getShared().getUserPresets().getFolder();
        folder.createDirectory();
        folder.revealToUser();
    };
    closeButton.onClick = [this] { if (onClose) onClose(); };
    for (auto* b : { &saveButton, &saveAsButton, &editButton, &deleteButton, &folderButton, &closeButton })
    {
        styleButton (*b);
        addAndMakeVisible (b);
    }
    setWantsKeyboardFocus (true);
    refresh();
}

PresetBrowser::~PresetBrowser() = default;

//==============================================================================
void PresetBrowser::refresh()
{
    auto& users = processor.getShared().getUserPresets();
    users.rescan();
    all.clear();
    categories.clear();
    processor.getShared().withLibrary ([&] (const IRLibrary& lib) {
        for (const auto& p : lib.getPresets())
        {
            PresetEntry e;
            e.key = p.id;
            e.name = p.name;
            e.category = p.category;
            e.number = p.displayNumber();
            e.irId = p.id;
            all.push_back (std::move (e));
        }
        categories = lib.getCategories();
    });
    for (auto& e : all)
    {
        e.tags = users.getLibraryTags (e.key);
        e.favourite = users.isFavourite (e.key);
    }
    for (const auto& u : users.getUserPresets())
    {
        all.push_back (u);
        categories.addIfNotAlreadyThere (u.category);
    }
    tags.clear();
    for (auto& e : all)
        for (auto& t : e.tags)
            tags.addIfNotAlreadyThere (t);
    tags.sortNatural();
    for (int i = selectedTags.size(); --i >= 0;)
        if (! tags.contains (selectedTags[i])) selectedTags.remove (i);
    rebuildFilterButtons();
    applyFilter();
}

void PresetBrowser::rebuildFilterButtons()
{
    categoryButtons.clear();
    for (auto& c : categories)
    {
        auto b = std::make_unique<TextButton> (c == UserPresetLibrary::kUserCategory ? String ("USER") : IRLibrary::shortCategoryName (c));
        styleButton (*b);
        b->getProperties().set ("led", true);
        b->setTooltip ("Only " + c + " (click again for all categories)");
        b->onClick = [this, c] { setCategory (category == c ? String() : c); };
        addAndMakeVisible (*b);
        categoryButtons.push_back (std::move (b));
    }
    tagButtons.clear();
    for (auto& t : tags)
    {
        auto b = std::make_unique<TextButton> (t);
        b->getProperties().set ("fontHeight", 10.5f);
        b->getProperties().set ("led", true);
        b->setTooltip ("Only presets tagged \"" + t + "\"");
        b->onClick = [this, t] { setTagSelected (t, ! selectedTags.contains (t)); };
        addAndMakeVisible (*b);
        tagButtons.push_back (std::move (b));
    }
    resized();
}

void PresetBrowser::setCategory (const String& c)
{
    category = c;
    applyFilter();
}

void PresetBrowser::setSearchText (const String& text)
{
    search.setText (text, false);
    applyFilter();
}

void PresetBrowser::setTagSelected (const String& tag, bool selected)
{
    if (selected) selectedTags.addIfNotAlreadyThere (tag);
    else selectedTags.removeString (tag);
    applyFilter();
}

void PresetBrowser::applyFilter()
{
    shown.clear();
    const auto text = search.getText();
    for (auto& e : all)
    {
        if (filter == kFavourites && ! e.favourite) continue;
        if (filter == kUser && ! e.user) continue;
        if (category.isNotEmpty() && e.category != category) continue;
        bool tagged = true;
        for (auto& t : selectedTags)
            tagged = tagged && e.tags.contains (t);
        if (! tagged || ! e.matches (text)) continue;
        shown.push_back (e);
    }

    allButton.setToggleState (filter == kAll, dontSendNotification);
    favButton.setToggleState (filter == kFavourites, dontSendNotification);
    userButton.setToggleState (filter == kUser, dontSendNotification);
    for (size_t i = 0; i < categoryButtons.size(); ++i)
        categoryButtons[i]->setToggleState (categories[(int) i] == category, dontSendNotification);
    for (size_t i = 0; i < tagButtons.size(); ++i)
        tagButtons[i]->setToggleState (selectedTags.contains (tags[(int) i]), dontSendNotification);

    list.updateContent();
    const ScopedValueSetter<bool> guard (selectingCurrent, true);
    const auto current = currentKey();
    list.deselectAllRows();
    for (size_t i = 0; i < shown.size(); ++i)
        if (shown[i].key == current)
        {
            list.selectRow ((int) i);
            break;
        }
    list.repaint();
    updateButtons();
}

String PresetBrowser::currentKey() const
{
    const auto user = processor.getUserPresetKey();
    return user.isNotEmpty() ? user : processor.getPresetId();
}

const PresetEntry* PresetBrowser::selectedEntry() const
{
    const int row = list.getSelectedRow();
    return row >= 0 && row < (int) shown.size() ? &shown[(size_t) row] : nullptr;
}

void PresetBrowser::updateButtons()
{
    const auto* e = selectedEntry();
    editButton.setEnabled (e != nullptr);
    deleteButton.setEnabled (e != nullptr && e->user);
    if (e == nullptr || e->key != deleteArmedKey)
    {
        deleteArmedKey.clear();
        deleteButton.setButtonText ("DELETE");
    }
    editButton.setButtonText (e != nullptr && ! e->user ? "TAGS..." : "EDIT...");
}

void PresetBrowser::opened()
{
    refresh();
    if (const int row = list.getSelectedRow(); row >= 0)
        list.scrollToEnsureRowIsOnscreen (row);
    search.grabKeyboardFocus();
}

void PresetBrowser::choose (int row)
{
    if (row < 0 || row >= (int) shown.size()) return;
    const auto e = shown[(size_t) row];
    if (e.user) processor.loadUserPreset (e.key);
    else processor.selectPreset (e.key, true);
    list.repaint();
    updateButtons();
}

void PresetBrowser::step (int delta)
{
    if (all.empty()) refresh();
    if (shown.empty()) return;
    const auto current = currentKey();
    int idx = -1;
    for (size_t i = 0; i < shown.size(); ++i)
        if (shown[i].key == current) idx = (int) i;
    const int n = (int) shown.size();
    idx = idx < 0 ? (delta > 0 ? 0 : n - 1) : ((idx + delta) % n + n) % n;
    {
        const ScopedValueSetter<bool> guard (selectingCurrent, true);
        list.selectRow (idx);
    }
    choose (idx);
}

//==============================================================================
void PresetBrowser::paintListBoxItem (int row, Graphics& g, int width, int height, bool selected)
{
    if (row < 0 || row >= (int) shown.size()) return;
    const auto& e = shown[(size_t) row];
    const bool current = e.key == currentKey();
    auto r = Rectangle<int> (0, 0, width, height).toFloat();
    if (current)
    {
        g.setColour (Colours::accentDim.withAlpha (0.55f));
        g.fillRect (r);
    }
    if (selected)
    {
        g.setColour (Colours::accent.withAlpha (current ? 0.9f : 0.5f));
        g.drawRect (r.reduced (1.0f), 1.0f);
    }

    drawStar (g, r.removeFromLeft ((float) kStarWidth).withSizeKeepingCentre (14.0f, 14.0f), e.favourite,
              e.favourite ? Colours::warning : Colours::textFaint);
    auto cat = r.removeFromLeft (120.0f);
    g.setColour (Colours::textDim);
    g.setFont (labelFont (10.0f));
    g.drawText (e.user ? (e.category == UserPresetLibrary::kUserCategory ? String ("USER") : e.category.toUpperCase())
                       : IRLibrary::shortCategoryName (e.category) + " " + e.number,
                cat, Justification::centredLeft);

    // right: tags, for user presets also the impulse response they use
    String detail = e.tags.joinIntoString ("  ");
    if (e.user && e.irId.isNotEmpty())
        detail = e.irId.fromLastOccurrenceOf ("/", false, false) + (detail.isNotEmpty() ? "  |  " + detail : String());
    g.setFont (uiFont (12.0f));
    const float detailW = std::min (r.getWidth() * 0.45f, (float) GlyphArrangement::getStringWidthInt (g.getCurrentFont(), detail) + 12.0f);
    g.setColour (Colours::textFaint);
    g.drawText (detail, r.removeFromRight (detailW).withTrimmedRight (10.0f), Justification::centredRight, true);

    g.setColour (current ? juce::Colours::white : Colours::text);
    g.setFont (uiFont (14.0f, current));
    g.drawText (e.name, r, Justification::centredLeft, true);
}

void PresetBrowser::listBoxItemClicked (int row, const MouseEvent& e)
{
    if (row < 0 || row >= (int) shown.size()) return;
    if (e.x < kStarWidth)
    {
        auto& entry = shown[(size_t) row];
        entry.favourite = ! entry.favourite;
        processor.getShared().getUserPresets().setFavourite (entry.key, entry.favourite);
        for (auto& a : all)
            if (a.key == entry.key) a.favourite = entry.favourite;
        list.repaintRow (row);
        return;
    }
    if (shown[(size_t) row].key != currentKey())
        choose (row);
}

void PresetBrowser::listBoxItemDoubleClicked (int row, const MouseEvent& e)
{
    if (e.x < kStarWidth) return;
    if (shown[(size_t) row].key != currentKey()) choose (row);
    if (onClose) onClose();
}

void PresetBrowser::selectedRowsChanged (int lastRow)
{
    updateButtons();
    // the arrow keys load presets (clicks load in listBoxItemClicked, after the star column is checked)
    if (selectingCurrent || ModifierKeys::currentModifiers.isAnyMouseButtonDown()) return;
    if (lastRow >= 0 && lastRow < (int) shown.size() && shown[(size_t) lastRow].key != currentKey())
        choose (lastRow);
}

void PresetBrowser::returnKeyPressed (int row)
{
    if (row >= 0 && row < (int) shown.size() && shown[(size_t) row].key != currentKey()) choose (row);
    if (onClose) onClose();
}

String PresetBrowser::getTooltipForRow (int row)
{
    if (row < 0 || row >= (int) shown.size()) return {};
    const auto& e = shown[(size_t) row];
    String tip = e.user ? "User preset, impulse response " + e.irId : e.category + " " + e.number + " " + e.name;
    if (e.notes.isNotEmpty()) tip << "\n" << e.notes;
    return tip + "\nClick the star to make it a favourite.";
}

bool PresetBrowser::keyPressed (const KeyPress& key)
{
    if (key == KeyPress::escapeKey)
    {
        if (editPanel != nullptr) editPanel.reset();
        else if (onClose) onClose();
        return true;
    }
    return false;
}

//==============================================================================
void PresetBrowser::save()
{
    auto& users = processor.getShared().getUserPresets();
    const auto key = processor.getUserPresetKey();
    const auto* entry = users.find (key);
    if (entry == nullptr)
    {
        showEditPanel (EditPanel::SaveAs);
        return;
    }
    String error;
    const auto name = entry->name;
    const auto entryTags = entry->tags;
    const auto entryNotes = entry->notes;
    processor.saveUserPreset (name, entryTags, entryNotes, error);
    refresh();
}

void PresetBrowser::showEditPanel (int mode)
{
    const auto m = (EditPanel::Mode) mode;
    const PresetEntry* entry = m == EditPanel::SaveAs ? processor.getShared().getUserPresets().find (processor.getUserPresetKey())
                                                       : selectedEntry();
    if (m != EditPanel::SaveAs && entry == nullptr) return;
    const auto status = processor.getStatus();
    const auto suggested = status.userPresetName.isNotEmpty() ? status.userPresetName : status.presetName;
    editPanel = std::make_unique<EditPanel> (m, entry, suggested);
    const auto key = entry != nullptr ? entry->key : String();
    editPanel->onCancel = [this] { editPanel.reset(); list.grabKeyboardFocus(); };
    editPanel->onOk = [this, m, key] {
        auto& users = processor.getShared().getUserPresets();
        const auto name = editPanel->name.getText();
        const auto newTags = UserPresetLibrary::parseTags (editPanel->tagsEditor.getText());
        const auto notes = editPanel->notes.getText();
        String error;
        if (m == EditPanel::SaveAs)
        {
            processor.saveUserPreset (name, newTags, notes, error);
        }
        else if (m == EditPanel::EditUser)
        {
            const auto newKey = users.edit (key, name, newTags, notes, error);
            if (newKey.isNotEmpty()) processor.userPresetRenamed (key, newKey);
        }
        else
        {
            users.setLibraryTags (key, newTags);
        }
        if (error.isNotEmpty())
        {
            editPanel->setError (error);
            return;
        }
        // the panel goes after this callback: it is the one running it
        MessageManager::callAsync ([safe = SafePointer<PresetBrowser> (this)] {
            if (safe == nullptr) return;
            safe->editPanel.reset();
            safe->refresh();
            safe->list.grabKeyboardFocus();
        });
    };
    addAndMakeVisible (*editPanel);
    editPanel->setBounds (getLocalBounds());
    editPanel->focus();
}

void PresetBrowser::deleteSelected()
{
    const auto* e = selectedEntry();
    if (e == nullptr || ! e->user) return;
    if (deleteArmedKey != e->key)
    {
        deleteArmedKey = e->key;
        deleteButton.setButtonText ("REALLY?");
        return;
    }
    const auto key = e->key;
    deleteArmedKey.clear();
    if (processor.getShared().getUserPresets().remove (key))
        processor.userPresetRenamed (key, {});
    refresh();
}

//==============================================================================
void PresetBrowser::paint (Graphics& g)
{
    g.fillAll (Colours::background);
    auto r = getLocalBounds().toFloat().reduced (12.0f, 10.0f);
    g.setColour (Colours::panel);
    g.fillRoundedRectangle (r, 8.0f);
    g.setColour (Colours::outline);
    g.drawRoundedRectangle (r.reduced (0.5f), 8.0f, 1.0f);

    g.setColour (Colours::accent.withAlpha (0.85f));
    g.setFont (labelFont (10.0f));
    g.drawText ("SHOW", sideArea.withHeight (14), Justification::topLeft);
    if (! categoryButtons.empty())
        g.drawText ("CATEGORY", categoryButtons.front()->getBounds().translated (0, -18).withHeight (14), Justification::bottomLeft);

    if (shown.empty())
    {
        g.setColour (Colours::textDim);
        g.setFont (uiFont (14.0f));
        g.drawText (all.empty() ? "No presets yet - install the impulse responses (SETUP)" : "No preset matches",
                    list.getBounds(), Justification::centred);
    }
}

void PresetBrowser::resized()
{
    auto r = getLocalBounds().reduced (12, 10).reduced (16, 14);
    sideArea = r.removeFromLeft (150);
    r.removeFromLeft (16);

    auto side = sideArea.withTrimmedTop (18);
    for (auto* b : { &allButton, &favButton, &userButton })
    {
        b->setBounds (side.removeFromTop (28));
        side.removeFromTop (5);
    }
    side.removeFromTop (22);
    for (auto& b : categoryButtons)
    {
        if (side.getHeight() < 26) { b->setVisible (false); continue; }
        b->setVisible (true);
        b->setBounds (side.removeFromTop (26));
        side.removeFromTop (4);
    }

    auto top = r.removeFromTop (32);
    search.setBounds (top.removeFromLeft (300));
    top.removeFromLeft (10);
    // tags in one row next to the search field; those that do not fit stay hidden (the search finds them)
    for (auto& b : tagButtons)
    {
        const int w = std::max (52, GlyphArrangement::getStringWidthInt (uiFont (12.0f), b->getButtonText()) + 34);
        if (top.getWidth() < w) { b->setVisible (false); continue; }
        b->setVisible (true);
        b->setBounds (top.removeFromLeft (w).reduced (0, 2));
        top.removeFromLeft (5);
    }

    r.removeFromTop (10);
    auto buttons = r.removeFromBottom (30);
    r.removeFromBottom (10);
    list.setBounds (r);
    for (auto* b : { &saveButton, &saveAsButton, &editButton, &deleteButton })
    {
        b->setBounds (buttons.removeFromLeft (104));
        buttons.removeFromLeft (8);
    }
    closeButton.setBounds (buttons.removeFromRight (104));
    buttons.removeFromRight (8);
    folderButton.setBounds (buttons.removeFromRight (104));
    if (editPanel != nullptr) editPanel->setBounds (getLocalBounds());
}
} // namespace concert7
