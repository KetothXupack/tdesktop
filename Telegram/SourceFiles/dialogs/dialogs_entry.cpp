/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "core/utils.h"
#include "dialogs/dialogs_entry.h"

#include "dialogs/dialogs_key.h"
#include "dialogs/dialogs_indexed_list.h"
#include "mainwidget.h"
#include "auth_session.h"
#include "styles/style_dialogs.h"
#include "history/history_item.h"
#include "history/history.h"

#include <QStringBuilder>
#include <QStandardPaths>
#include <ctime>
#include <iostream>
#include <set>
#include <fstream>
#include <limits>
#include <ios>
#include <memory>
#include <string>


namespace Dialogs {
namespace {

auto DialogsPosToTopShift = 0;

const uint64 OLD_MESSAGE = 604800; // 1 week
const std::string PEERS_CONFIG_FILE =
        (QStandardPaths::writableLocation(QStandardPaths::ConfigLocation) % "/telegram_peers.conf").toUtf8().constData();

const auto STREAM_MAX_SIZE = std::numeric_limits<std::streamsize>::max();

std::atomic<std::set<PeerId>*> soft_pinned_peers;

// 0. promoted:
//     0xFFFFFFFFFFFF0001
// 1. pinned dialog:
//     0xFFFFFFFF00000000 - 0xFFFFFFFFFFFFFFFF
// 2. un-muted and unread or message with unread mention dialogs:
//     0xD000000000000000 - 0xDFFFFFFFF0000000
// 3. un-muted (and read) dialogs age <= 1w:
//     0xC000000000000000 - 0xCFFFFFFFF0000000
// 4. muted dialog:
//     0xB000000000000000 - 0xBFFFFFFFF0000000
// 3. un-muted (and read) dialogs age >= 1w:
//     0xA000000000000000 - 0xAFFFFFFFF0000000
uint64 DialogPosFromDateAndCategory(TimeId date, EntryCategory category) {
	if (!date) {
		return 0;
	}
	return ((static_cast<uint64>(category) << 60) + (uint64(date) << 28)) | (++DialogsPosToTopShift);
}

uint64 ProxyPromotedDialogPos() {
	return 0xFFFFFFFFFFFF0001ULL;
}

uint64 PinnedDialogPos(int pinnedIndex) {
	return 0xFFFFFFFF00000000ULL + pinnedIndex;
}

} // namespace

Entry::Entry(const Key &key)
: lastItemTextCache(st::dialogsTextWidthMin)
, _key(key) {
}

void Entry::cachePinnedIndex(int index) {
	if (_pinnedIndex != index) {
		const auto wasPinned = isPinnedDialog();
		_pinnedIndex = index;
		updateChatListEntry();
		if (wasPinned != isPinnedDialog()) {
			changedChatListPinHook();
		}
	}
}

void Entry::cacheProxyPromoted(bool promoted) {
	if (_isProxyPromoted != promoted) {
		_isProxyPromoted = promoted;
		updateChatListEntry();
		if (!_isProxyPromoted) {
			updateChatListExistence();
		}
	}
}

bool Entry::needUpdateInChatList() const {
	return inChatList(Dialogs::Mode::All) || shouldBeInChatList();
}

void Entry::calculateChatListSortPosition() {
//	auto sortKeyInChatList = _sortKeyInChatList;

	_sortKeyInChatList = isPinnedDialog()
		? PinnedDialogPos(_pinnedIndex)
		: DialogPosFromDateAndCategory(adjustChatListTimeId(), _messageCategory);

//	std::cout
//		<< "calculateChatListSortPosition [" << chatsListName() << "]"
//		<< ": sortKeyInChatList=" << sortKeyInChatList
//		<< ", _sortKeyInChatList=" << sortKeyInChatList
//		<< ", _prioritized=" << _prioritized
//		<< std::endl;
}

uint64 Entry::sortKeyInChatList() {
	auto priorityChanged = updatePriority();
	calculateChatListSortPosition();

	if (priorityChanged) {
		setChatListExistence(true);
	}

	return _sortKeyInChatList;
}

void lazyLoadSoftlyPinnedPeers() {
	if (!soft_pinned_peers.load()) {
		std::cout << "Loading soft pinned peers list from " << PEERS_CONFIG_FILE << "..." << std::endl;
		auto newPeers = new std::set<PeerId>();
		std::ifstream in(PEERS_CONFIG_FILE);
		while (in.is_open()) {
			PeerId peerId;
			if (!(in >> peerId)) {
				break;
			}

			std::cout << "Peer id " << peerId << " added to soft pinned peers list" << std::endl;
			in.ignore(STREAM_MAX_SIZE, '\n');
			newPeers->insert(peerId);
		}
		in.close();

		soft_pinned_peers.store(newPeers);
		std::cout << "Loaded " << newPeers->size() << " soft pinned peers!" << std::endl;
	}
}

bool Entry::updatePriority() {
	lazyLoadSoftlyPinnedPeers();

	auto messageCategory = EntryCategory::BOTTOM;
	const auto _historyItem = chatsListItem();
	if (_historyItem) {
		auto _history = _historyItem->history();
		auto _muted = _history
			? _history->mute()
			: false;
		auto _unreadMention = _history
			? _history->hasUnreadMentions()
			: false;
		auto peerId = _history
			? _history->peer->id
			: 0;

		auto _unreadCount = chatListUnreadCount(); // may be -1 if chat list not loaded

		auto messageAge = unixtime() - _lastMessageTimeId;
		if (soft_pinned_peers.load()->count(peerId) != 0) {
			messageCategory = EntryCategory::SOFT_PINNED;
		} else if ((_unreadCount > 0 && !_muted) || _unreadMention || chatListUnreadMark()) {
			messageCategory = EntryCategory::UNMUTED_UNREAD;
		} else if (!_muted && _unreadCount == 0 && messageAge <= OLD_MESSAGE) {
			messageCategory = EntryCategory::UNMUTED_READ_YOUNG;
		} else if (!_muted && _unreadCount == 0 && messageAge > OLD_MESSAGE) {
			messageCategory = EntryCategory::UNMUTED_READ_OLD;
		} else if (_muted) {
			messageCategory = EntryCategory::MUTED;
		}

//		std::cout
//			<< "updatePriority [" << chatsListName() << "]"
//			<< ": muted=" << _muted
//			<< ", unreadCount=" << _unreadCount
//			<< ", unreadCount=" << _unreadCount
//			<< ", unreadMark=" << chatListUnreadMark()
//			<< std::endl;
	}

	bool result = (_messageCategory != messageCategory);
	_messageCategory = messageCategory;
	return result;
}

void Entry::updateChatListExistence() {
	setChatListExistence(shouldBeInChatList());
}

void Entry::setChatListExistence(bool exists) {
	if (const auto main = App::main()) {
		if (exists && _sortKeyInChatList) {
			main->createDialog(_key);
			updateChatListEntry();
		} else {
			main->removeDialog(_key);
		}
	}
}

TimeId Entry::adjustChatListTimeId() const {
	return chatsListTimeId();
}

void Entry::changedInChatListHook(Dialogs::Mode list, bool added) {
}

void Entry::changedChatListPinHook() {
}

RowsByLetter &Entry::chatListLinks(Mode list) {
	return _chatListLinks[static_cast<int>(list)];
}

const RowsByLetter &Entry::chatListLinks(Mode list) const {
	return _chatListLinks[static_cast<int>(list)];
}

Row *Entry::mainChatListLink(Mode list) const {
	auto it = chatListLinks(list).find(0);
	Assert(it != chatListLinks(list).cend());
	return it->second;
}

PositionChange Entry::adjustByPosInChatList(
		Mode list,
		not_null<IndexedList*> indexed) {
	const auto lnk = mainChatListLink(list);
	const auto movedFrom = lnk->pos();
	indexed->adjustByPos(chatListLinks(list));
	const auto movedTo = lnk->pos();
	return { movedFrom, movedTo };
}

void Entry::setChatsListTimeId(TimeId date) {
	if (_lastMessageTimeId && _lastMessageTimeId >= date) {
		if (!inChatList(Dialogs::Mode::All)) {
			return;
		}
	}
	_lastMessageTimeId = date;
	updateChatListEntry();
}

int Entry::posInChatList(Dialogs::Mode list) const {
	return mainChatListLink(list)->pos();
}

not_null<Row*> Entry::addToChatList(
		Mode list,
		not_null<IndexedList*> indexed) {
	if (!inChatList(list)) {
		chatListLinks(list) = indexed->addToEnd(_key);
		changedInChatListHook(list, true);
	}
	return mainChatListLink(list);
}

void Entry::removeFromChatList(
		Dialogs::Mode list,
		not_null<Dialogs::IndexedList*> indexed) {
	if (inChatList(list)) {
		indexed->del(_key);
		chatListLinks(list).clear();
		changedInChatListHook(list, false);
	}
}

void Entry::removeChatListEntryByLetter(Mode list, QChar letter) {
	Expects(letter != 0);

	if (inChatList(list)) {
		chatListLinks(list).remove(letter);
	}
}

void Entry::addChatListEntryByLetter(
		Mode list,
		QChar letter,
		not_null<Row*> row) {
	Expects(letter != 0);

	if (inChatList(list)) {
		chatListLinks(list).emplace(letter, row);
	}
}

void Entry::updateChatListEntry() {
	updatePriority();
	calculateChatListSortPosition();

	if (const auto main = App::main()) {
		if (needUpdateInChatList() && _sortKeyInChatList) {
			main->createDialog(_key);
		}

		if (inChatList(Mode::All)) {
			main->repaintDialogRow(
				Mode::All,
				mainChatListLink(Mode::All));
			if (inChatList(Mode::Important)) {
				main->repaintDialogRow(
					Mode::Important,
					mainChatListLink(Mode::Important));
			}
		}
	}
}

} // namespace Dialogs
