/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "dialogs/dialogs_row.h"

#include "styles/style_dialogs.h"
#include "ui/effects/ripple_animation.h"
#include "dialogs/dialogs_entry.h"
// #include "history/history_item.h"
#include "history/history.h"
#include "mainwidget.h"

namespace Dialogs {

RippleRow::RippleRow() = default;
RippleRow::~RippleRow() = default;

void RippleRow::addRipple(QPoint origin, QSize size, Fn<void()> updateCallback) {
	if (!_ripple) {
		auto mask = Ui::RippleAnimation::rectMask(size);
		_ripple = std::make_unique<Ui::RippleAnimation>(st::dialogsRipple, std::move(mask), std::move(updateCallback));
	}
	_ripple->add(origin);
}

void RippleRow::stopLastRipple() {
	if (_ripple) {
		_ripple->lastStop();
	}
}

void RippleRow::paintRipple(Painter &p, int x, int y, int outerWidth, TimeMs ms, const QColor *colorOverride) const {
	if (_ripple) {
		_ripple->paint(p, x, y, outerWidth, ms, colorOverride);
		if (_ripple->empty()) {
			_ripple.reset();
		}
	}
}

uint64 Row::sortKey() const {
	auto _entry = _id.entry();

//	auto _history       = _id.history();
//	auto muted          = _history ? _history->chatListMutedBadge() : false;
//	auto unreadMention  = _history ? _history->hasUnreadMentions() : false;
//	auto unreadCount    = _entry ? _entry->chatListUnreadCount() : 0;
//	_entry->updatePriority((unreadCount > 0 && !muted) || unreadMention);

	return _entry->sortKeyInChatList();
}

FakeRow::FakeRow(Key searchInChat, not_null<HistoryItem*> item)
: _searchInChat(searchInChat)
, _item(item)
, _cache(st::dialogsTextWidthMin) {
}

} // namespace Dialogs