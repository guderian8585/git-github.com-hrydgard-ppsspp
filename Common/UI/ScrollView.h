#pragma once

#include "Common/UI/View.h"
#include "Common/UI/ViewGroup.h"

namespace UI {

// A scrollview usually contains just a single child - a linear layout or similar.
class ScrollView : public ViewGroup {
public:
	ScrollView(Orientation orientation, LayoutParams *layoutParams = 0)
		: ViewGroup(layoutParams), orientation_(orientation) {}
	~ScrollView() override;

	void Measure(const UIContext &dc, MeasureSpec horiz, MeasureSpec vert) override;
	void Layout() override;

	bool Key(const KeyInput &input) override;
	bool Touch(const TouchInput &input) override;
	void Draw(UIContext &dc) override;
	std::string DescribeLog() const override { return "ScrollView: " + View::DescribeLog(); }

	void ScrollTo(float newScrollPos);
	void ScrollToBottom();
	void ScrollRelative(float distance);
	bool CanScroll() const;
	void Update() override;

	void RememberPosition(float *pos) {
		rememberPos_ = pos;
		ScrollTo(*pos);
	}

	// Get the last moved scroll view position
	static void GetLastScrollPosition(float &x, float &y);

	// Override so that we can scroll to the active one after moving the focus.
	bool SubviewFocused(View *view) override;
	void PersistData(PersistStatus status, std::string anonId, PersistMap &storage) override;
	void SetVisibility(Visibility visibility) override;

	// If the view is smaller than the scroll view, sets whether to align to the bottom/right instead of the left.
	void SetAlignOpposite(bool alignOpposite) {
		alignOpposite_ = alignOpposite;
	}

	NeighborResult FindScrollNeighbor(View *view, const Point &target, FocusDirection direction, NeighborResult best) override;

private:
	float HardClampedScrollPos(float pos) const;

	// TODO: Don't adjust pull_ within this!
	float ClampedScrollPos(float pos);

	// The "bob" is the draggable thingy on a scroll bar. Don't know a better name for it.
	struct Bob {
		bool show;
		float thickness;
		float size;
		float offset;
		float scrollMax;
	};

	Bob ComputeBob() const;

	GestureDetector gesture_;
	Orientation orientation_;
	float scrollPos_ = 0.0f;
	float scrollStart_ = 0.0f;
	float scrollTarget_ = 0.0f;
	int scrollTouchId_ = -1;
	bool scrollToTarget_ = false;
	float layoutScrollPos_ = 0.0f;
	float inertia_ = 0.0f;
	float pull_ = 0.0f;
	float lastViewSize_ = 0.0f;
	float *rememberPos_ = nullptr;
	bool alignOpposite_ = false;
	bool draggingBob_ = false;

	float barDragStart_ = 0.0f;
	float barDragOffset_ = 0.0f;

	static float lastScrollPosX;
	static float lastScrollPosY;
};

// Yes, this feels a bit Java-ish...
class ListAdaptor {
public:
	virtual ~ListAdaptor() {}
	virtual View *CreateItemView(int index, ImageID *optionalImageID) = 0;
	virtual int GetNumItems() = 0;
	virtual bool AddEventCallback(View *view, std::function<EventReturn(EventParams &)> callback) { return false; }
	virtual std::string GetTitle(int index) const { return ""; }
	virtual void SetSelected(int sel) { }
	virtual int GetSelected() { return -1; }
};

class ChoiceListAdaptor : public ListAdaptor {
public:
	ChoiceListAdaptor(const char *items[], int numItems) : items_(items), numItems_(numItems) {}
	View *CreateItemView(int index, ImageID *optionalImageID) override;
	int GetNumItems() override { return numItems_; }
	bool AddEventCallback(View *view, std::function<EventReturn(EventParams &)> callback) override;

private:
	const char **items_;
	int numItems_;
};

// The "selected" item is what was previously selected (optional). This items will be drawn differently.
class StringVectorListAdaptor : public ListAdaptor {
public:
	StringVectorListAdaptor() : selected_(-1) {}
	StringVectorListAdaptor(const std::vector<std::string> &items, int selected = -1) : items_(items), selected_(selected) {}
	View *CreateItemView(int index, ImageID *optionalImageID) override;
	int GetNumItems() override { return (int)items_.size(); }
	bool AddEventCallback(View *view, std::function<EventReturn(EventParams &)> callback) override;
	void SetSelected(int sel) override { selected_ = sel; }
	std::string GetTitle(int index) const override { return items_[index]; }
	int GetSelected() override { return selected_; }

private:
	std::vector<std::string> items_;
	int selected_;
};

// A list view is a scroll view with autogenerated items.
// In the future, it might be smart and load/unload items as they go, but currently not.
class ListView : public ScrollView {
public:
	ListView(ListAdaptor *a, std::set<int> hidden = std::set<int>(), std::map<int, ImageID> icons = std::map<int, ImageID>(), LayoutParams *layoutParams = 0);

	int GetSelected() { return adaptor_->GetSelected(); }
	void Measure(const UIContext &dc, MeasureSpec horiz, MeasureSpec vert) override;
	virtual void SetMaxHeight(float mh) { maxHeight_ = mh; }
	Event OnChoice;
	std::string DescribeLog() const override { return "ListView: " + View::DescribeLog(); }
	std::string DescribeText() const override;

private:
	void CreateAllItems();
	EventReturn OnItemCallback(int num, EventParams &e);
	ListAdaptor *adaptor_;
	LinearLayout *linLayout_;
	float maxHeight_;
	std::set<int> hidden_;
	std::map<int, ImageID> icons_;
};

}  // namespace UI
