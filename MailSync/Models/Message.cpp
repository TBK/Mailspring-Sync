//
//  Message.cpp
//  MailSync
//
//  Created by Ben Gotow on 6/17/17.
//  Copyright © 2017 Foundry 376. All rights reserved.
//

#include "Message.hpp"
#include "MailUtils.hpp"
#include "Folder.hpp"

using namespace std;

string Message::TABLE_NAME = "Message";

Message::Message(mailcore::IMAPMessage * msg, Folder & folder) :
MailModel(MailUtils::idForMessage(msg), folder.accountId(), 0)
{
    _folderId = folder.id();
    _date = (double)msg->header()->receivedDate();
    _headerMessageId = msg->header()->messageID()->UTF8Characters();
    _subject = msg->header()->subject()->UTF8Characters();
    _folderImapUID = msg->uid();
        
    mailcore::String * mgMsgId = msg->header()->extraHeaderValueForName(MCSTR("X-GM-MSGID"));
    if (mgMsgId) {
        _gMsgId = mgMsgId->UTF8Characters();
    }
}

Message::Message(SQLite::Statement & query) :
    MailModel(query),
    _date(query.getColumn("date").getDouble()),
    _unread(query.getColumn("unread").getInt()),
    _starred(query.getColumn("starred").getInt()),
    _subject(query.getColumn("subject").getString()),
    _headerMessageId(query.getColumn("headerMessageId").getString()),
    _folderImapUID(query.getColumn("folderImapUID").getUInt()),
    _folderId(query.getColumn("folderId").getString()),
    _threadId(query.getColumn("threadId").getString()),
    _gMsgId(query.getColumn("gMsgId").getString())
{

}

bool Message::isUnread() {
    return _unread;
}

void Message::setUnread(bool u) {
    _unread = u;
}

void Message::setStarred(bool s) {
    _starred = s;
}

bool Message::isStarred() {
    return _starred;
}

double Message::date() {
    return _date;
}

string Message::subject() {
    return _subject;
}

void Message::setThreadId(string threadId) {
    _threadId = threadId;
}

string Message::getHeaderMessageId() {
    return _headerMessageId;
}

string Message::tableName() {
    return Message::TABLE_NAME;
}

vector<string> Message::columnsForQuery() {
    return vector<string>{"id", "data", "accountId", "version", "headerMessageId", "subject", "gMsgId", "date", "draft", "isSent", "unread", "starred", "folderImapUID", "folderImapXGMLabels", "folderId", "threadId"};
}

void Message::bindToQuery(SQLite::Statement & query) {
    MailModel::bindToQuery(query);
    query.bind(":date", _date);
    query.bind(":unread", _unread);
    query.bind(":starred", _starred);
    query.bind(":headerMessageId", _headerMessageId);
    query.bind(":subject", _subject);
    query.bind(":folderImapUID", _folderImapUID);
    query.bind(":folderId", _folderId);
    query.bind(":threadId", _threadId);
    query.bind(":gMsgId", _gMsgId);
}

json Message::toJSON()
{
    return MailUtils::merge(MailModel::toJSON(), {
        {"object", "message"},
        {"date", _date},
        {"unread", _unread},
        {"starred", _starred},
        {"headerMessageId", _headerMessageId},
        {"subject", _subject},
        {"folderId", _folderId},
        {"threadId", _threadId}
    });
}

