// Dialog/PhoneScroll.rsc -- ext.phone_menu's scrolling frame for a page retail does not scroll
// (NOT RETAIL; P11b, docs/android/P11B_SCROLL_SIZE.md).
//
// The owner's ruling (P11): the Enhanced tab's fit, a scrolling list inside the page, is the model for
// every Options page; the Professions list scrolls too. src/ext/phone_menu/touch_scroll.cpp builds one
// of these around the Options dialog's Video and Audio pages and the Professions dialog's table on the
// phone (the row on) and MOVES the page into it (the controllers keep their pointers). The frame is the
// Game page's own container (Options.rsc:gameOptions: its padding, its scroll bar), with its size
// left to its content: _width / _height 0 (the description's own default height is 100). The height
// the frame scrolls at is set at run time (touch_scroll.h (b)); the empty group is replaced by the page.
ContainerDescription phoneScroll : "Options.rsc:gameOptions"
{
	int _width = 0;
	int _height = 0;
	ElementDescription _content = "phoneScrollContent";
}

GroupDescription phoneScrollContent
{
	ElementDescription _elements [ ]
}
