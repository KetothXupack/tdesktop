/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "core/utils.h"
#include "dialogs/dialogs_entry.h"

#include "base/unixtime.h"
#include "dialogs/dialogs_key.h"
#include "dialogs/dialogs_indexed_list.h"
#include "data/data_session.h"
#include "data/data_folder.h"
#include "mainwidget.h"
#include "main/main_session.h"
#include "history/history_item.h"
#include "history/history.h"
#include "styles/style_dialogs.h" // st::dialogsTextWidthMin

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

uint64 DialogPosFromDate(TimeId date) {
	if (!date) {
		return 0;
	}
	return (uint64(date) << 32) | (++DialogsPosToTopShift);
}

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

uint64 FixedOnTopDialogPos(int index) {
	return 0xFFFFFFFFFFFF000FULL - index;
}

uint64 PinnedDialogPos(int pinnedIndex) {
	return 0xFFFFFFFF000000FFULL - pinnedIndex;
}

} // namespace

Entry::Entry(not_null<Data::Session*> owner, const Key &key)
: lastItemTextCache(st::dialogsTextWidthMin)
, _owner(owner)
, _key(key) {
}

Data::Session &Entry::owner() const {
	return *_owner;
}

Main::Session &Entry::session() const {
	return _owner->session();
}

void Entry::cachePinnedIndex(int index) {
	if (_pinnedIndex != index) {
		const auto wasPinned = isPinnedDialog();
		_pinnedIndex = index;
		if (session().supportMode()) {
			// Force reorder in support mode.
			_sortKeyInChatList = 0;
		}
		updateChatListSortPosition();
		updateChatListEntry();
		if (wasPinned != isPinnedDialog()) {
			changedChatListPinHook();
		}
	}
}

void Entry::cacheProxyPromoted(bool promoted) {
	if (_isProxyPromoted != promoted) {
		_isProxyPromoted = promoted;
		updateChatListSortPosition();
		updateChatListEntry();
		if (!_isProxyPromoted) {
			updateChatListExistence();
		}
	}
}

bool Entry::needUpdateInChatList() const {
	return inChatList() || shouldBeInChatList();
}

void Entry::updateChatListSortPosition() {
	sortKeyInChatList();

	if (needUpdateInChatList()) {
		setChatListExistence(true);
	} else {
		_sortKeyInChatList = 0;
	}
}

uint64 Entry::sortKeyInChatList() {
	calculateChatListSortPosition();
	return _sortKeyInChatList;
}

bool Entry::calculateChatListSortPosition() {
	const auto changed = updatePriority();
	const auto fixedIndex = fixedOnTopIndex();
	_sortKeyInChatList = fixedIndex
		? FixedOnTopDialogPos(fixedIndex)
		: isPinnedDialog()
		? PinnedDialogPos(_pinnedIndex)
		: DialogPosFromDateAndCategory(adjustedChatListTimeId(), __messageCategory);

	auto inChatList = shouldBeInChatList();
	auto links = chatListLinks(Mode::All);
	if (links.find(0) != links.cend() && __updateNeeded) {
		__updateNeeded = false;
		setChatListExistence(true);
	} else if (!inChatList) {
		_sortKeyInChatList = 0;
	}

	return changed;
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

	auto messageCategory = __messageCategory;
	auto unreadMention = __unreadMention;
	auto muted = __muted;
	auto unreadCount = __unreadCount;

	auto _history = _key ? _key.history() : nullptr;
	if (_history && _history->lastMessageKnown()) {
		muted = _history
			? _history->mute()
			: false;
		unreadMention = _history
			? _history->hasUnreadMentions()
			: false;
		auto peerId = _history
			? _history->peer->id
			: 0;

		unreadCount = chatListUnreadCount(); // may be -1 if chat list not loaded
		auto messageAge = base::unixtime::now() - _timeId;
		if (soft_pinned_peers.load()->count(peerId) != 0) {
			messageCategory = EntryCategory::SOFT_PINNED;
		} else if ((__unreadCount > 0 && !__muted) || __unreadMention || chatListUnreadMark()) {
			messageCategory = EntryCategory::UNMUTED_UNREAD;
		} else if (!__muted && __unreadCount == 0 && messageAge <= OLD_MESSAGE) {
			messageCategory = EntryCategory::UNMUTED_READ_YOUNG;
		} else if (!__muted && __unreadCount == 0 && messageAge > OLD_MESSAGE) {
			messageCategory = EntryCategory::UNMUTED_READ_OLD;
		} else if (__muted) {
			messageCategory = EntryCategory::MUTED;
		}
	}

	// we want to repaint if any of this parameters was changed
	bool result = (__messageCategory != messageCategory);
	result |= (__unreadMention != unreadMention);
	result |= (__muted != muted);
	result |= (__unreadCount != unreadCount);

	__updateNeeded |= result;

	__messageCategory = messageCategory;
	__unreadMention = unreadMention;
	__muted = muted;
	__unreadCount = unreadCount;
	return result;
}

void Entry::updateChatListExistence() {
	setChatListExistence(shouldBeInChatList());
}

void Entry::notifyUnreadStateChange(const UnreadState &wasState) {
	owner().unreadStateChanged(_key, wasState);
}

void Entry::setChatListExistence(bool exists) {
	if (const auto main = App::main()) {
		if (exists && _sortKeyInChatList) {
			main->refreshDialog(_key);
			updateChatListEntry();
		} else {
			main->removeDialog(_key);
		}
	}
}

TimeId Entry::adjustedChatListTimeId() const {
	return chatListTimeId();
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
	auto links = chatListLinks(list);
	auto it = links.find(0);
	Assert(it != links.cend());
	return it->second;
}

PositionChange Entry::adjustByPosInChatList(Mode list) {
	const auto lnk = mainChatListLink(list);
	const auto from = lnk->pos();
	myChatsList(list)->adjustByDate(chatListLinks(list));
	const auto to = lnk->pos();
	return { from, to };
}

void Entry::setChatListTimeId(TimeId date) {
	_timeId = date;
	updateChatListSortPosition();
	if (const auto folder = this->folder()) {
		folder->updateChatListSortPosition();
	}
}

int Entry::posInChatList(Dialogs::Mode list) const {
	return mainChatListLink(list)->pos();
}

not_null<Row*> Entry::addToChatList(Mode list) {
	if (!inChatList(list)) {
		chatListLinks(list) = myChatsList(list)->addToEnd(_key);
		if (list == Mode::All) {
			owner().unreadEntryChanged(_key, true);
		}
	}
	return mainChatListLink(list);
}

void Entry::removeFromChatList(Dialogs::Mode list) {
	if (inChatList(list)) {
		myChatsList(list)->del(_key);
		chatListLinks(list).clear();
		if (list == Mode::All) {
			owner().unreadEntryChanged(_key, false);
		}
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
	calculateChatListSortPosition();

	if (const auto main = App::main()) {
		if (inChatList()) {
			main->repaintDialogRow(
				Mode::All,
				mainChatListLink(Mode::All));
			if (inChatList(Mode::Important)) {
				main->repaintDialogRow(
					Mode::Important,
					mainChatListLink(Mode::Important));
			}
		}
		if (session().supportMode()
			&& !session().settings().supportAllSearchResults()) {
			main->repaintDialogRow({ _key, FullMsgId() });
		}
	}
}

not_null<IndexedList*> Entry::myChatsList(Mode list) const {
	return owner().chatsList(folder())->indexed(list);
}

} // namespace Dialogs
