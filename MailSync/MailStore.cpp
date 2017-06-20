//
//  MailStore.cpp
//  MailSync
//
//  Created by Ben Gotow on 6/15/17.
//  Copyright © 2017 Foundry 376. All rights reserved.
//

#include "MailStore.hpp"
#include "MailUtils.hpp"

#import "Folder.hpp"
#import "Message.hpp"
#import "Thread.hpp"

using namespace mailcore;

MailStore::MailStore() :
    _db("/Users/bengotow/.nylas-dev/edgehill.db", SQLite::OPEN_READWRITE | SQLite::OPEN_CREATE)
{
    SQLite::Statement mQuery(this->_db, "CREATE TABLE IF NOT EXISTS Message ("
                             "id VARCHAR(65) PRIMARY KEY,"
                             "accountId VARCHAR(65),"
                             "version INTEGER,"
                             "data TEXT,"
                             "headerMessageId VARCHAR(255),"
                             "gMsgId VARCHAR(255),"
                             "gThrId VARCHAR(255),"
                             "subject VARCHAR(500),"
                             "date DATETIME,"
                             "draft TINYINT(1),"
                             "isSent TINYINT(1),"
                             "unread TINYINT(1),"
                             "starred TINYINT(1),"
                             "replyToMessageId VARCHAR(255),"
                             "folderImapUID INTEGER,"
                             "folderImapXGMLabels TEXT,"
                             "folderId VARCHAR(65),"
                             "threadId VARCHAR(65))");
    mQuery.exec();
    
    
    SQLite::Statement fQuery(this->_db, "CREATE TABLE IF NOT EXISTS Folder ("
                             "id VARCHAR(65) PRIMARY KEY,"
                             "accountId VARCHAR(65),"
                             "version INTEGER,"
                             "data TEXT,"
                             "path VARCHAR(255),"
                             "role VARCHAR(255),"
                             "localStatus TEXT,"
                             "createdAt DATETIME,"
                             "updatedAt DATETIME)");
    fQuery.exec();
    
    SQLite::Statement tQuery(this->_db, "CREATE TABLE IF NOT EXISTS Thread ("
                             "id VARCHAR(65) PRIMARY KEY,"
                             "accountId VARCHAR(65),"
                             "version INTEGER,"
                             "data TEXT,"
                             "subject VARCHAR(500),"
                             "snippet VARCHAR(255),"
                             "unread INTEGER,"
                             "starred INTEGER,"
                             "firstMessageTimestamp DATETIME,"
                             "lastMessageTimestamp DATETIME,"
                             "lastMessageReceivedTimestamp DATETIME,"
                             "lastMessageSentTimestamp DATETIME,"
                             "inAllMail TINYINT(1),"
                             "isSearchIndexed TINYINT(1),"
                             "participants TEXT,"
                             "hasAttachments INTEGER)");
    tQuery.exec();
    
    // TODO add unique keys

    SQLite::Statement tfQuery(this->_db, "CREATE TABLE IF NOT EXISTS ThreadFolder ("
                              "id VARCHAR(65) PRIMARY KEY,"
                              "value VARCHAR(65),"
                              "inAllMail TINYINT(1),"
                              "lastMessageReceivedTimestamp DATETIME,"
                              "lastMessageSentTimestamp DATETIME,"
                              "categoryId VARCHAR(65))");
    tfQuery.exec();

    SQLite::Statement tlQuery(this->_db, "CREATE TABLE IF NOT EXISTS ThreadLabel ("
                              "id VARCHAR(65) PRIMARY KEY,"
                              "value VARCHAR(65),"
                              "inAllMail TINYINT(1),"
                              "lastMessageReceivedTimestamp DATETIME,"
                              "lastMessageSentTimestamp DATETIME,"
                              "categoryId VARCHAR(65))");
    tlQuery.exec();
    
    SQLite::Statement trQuery(this->_db, "CREATE TABLE IF NOT EXISTS ThreadReference ("
                             "id INTEGER PRIMARY KEY,"
                             "threadId VARCHAR(65),"
                             "headerMessageId VARCHAR(255))");
    trQuery.exec();
}


std::map<std::string, Folder *> MailStore::getFoldersById() {
    SQLite::Statement query(this->_db, "SELECT * FROM Folder");
    std::map<std::string, Folder *> results{};
    while (query.executeStep()) {
        results[query.getColumn("id").getString()] = new Folder(query);
    }
    return results;
}

void MailStore::insertMessage(IMAPMessage * mMsg, Folder & folder) {
    Message msg(mMsg, folder);
    
    // first, find or build a thread for this message
    String * gThrId = mMsg->header()->extraHeaderValueForName(new String("X-GM-THRID"));
    Array * references = mMsg->header()->references();
    if (references == NULL) {
        references = new Array();
        references->autorelease();
    }
    Thread * thread = NULL;
    
    if (gThrId) {
        SQLite::Statement tQuery(this->_db, "SELECT * FROM Thread WHERE gThrId = ? LIMIT 1");
        tQuery.bind(1, gThrId->UTF8Characters());
        if (tQuery.executeStep()) {
            thread = new Thread(tQuery);
        }
    } else {
        // find an existing thread using the references
        std::string qmarks{"?"};
        for (int i = 0; i < references->count(); i ++) {
            qmarks = qmarks + ",?";
        }
        SQLite::Statement tQuery(this->_db, "SELECT Thread.* FROM Thread INNER JOIN ThreadReference ON ThreadReference.threadId = Thread.id WHERE ThreadReference.headerMessageId IN ("+qmarks+") LIMIT 1");
        tQuery.bind(1, msg.getHeaderMessageId());
        for (int i = 0; i < references->count(); i ++) {
            String * ref = (String *)references->objectAtIndex(i);
            tQuery.bind(2 + i, ref->UTF8Characters());
        }
        if (tQuery.executeStep()) {
            thread = new Thread(tQuery);
        }
    }
    
    if (thread) {
        thread->addMessage(msg);
        this->save(thread);
    } else {
        thread = new Thread(msg);
        this->save(thread);
    }
    
    thread->upsertReferences(this->_db, msg.getHeaderMessageId(), references);
    
    msg.setThreadId(thread->id());
    save(&msg);
}

std::map<uint32_t, MessageAttributes> MailStore::fetchMessagesAttributesInRange(Range range, Folder & folder) {
    SQLite::Statement query(this->_db, "SELECT id, version, unread, starred, folderImapUID, folderImapXGMLabels FROM Message WHERE folderId = ? AND folderImapUID >= ? AND folderImapUID <= ?");
    query.bind(1, folder.id());
    query.bind(2, (long long)(range.location));
    query.bind(3, (long long)(range.location + range.length));
    
    std::map<uint32_t, MessageAttributes> results {};

    while (query.executeStep()) {
        MessageAttributes attrs{};
        uint32_t uid = (uint32_t)query.getColumn("folderImapUID").getInt64();
        attrs.uid = uid;
        attrs.version = query.getColumn("version").getInt();
        attrs.flagged = query.getColumn("starred").getInt() != 0;
        attrs.seen = query.getColumn("unread").getInt() == 0;
        results[uid] = attrs;
    }
    
    return results;
}

void MailStore::updateMessageAttributes(MessageAttributes local, IMAPMessage * remoteMsg, Folder & folder) {
    std::cout << "\n";
    std::cout << remoteMsg->flags();
    bool remoteFlagged = remoteMsg->flags() & MessageFlagFlagged;
    bool remoteSeen = remoteMsg->flags() & MessageFlagSeen;
    
    if ((local.flagged != remoteFlagged) || (local.seen != remoteSeen)) {
        std::cout << "\nUpdating attributes for one message.";
        SQLite::Statement query(this->_db, "UPDATE Message SET unread = ?, starred = ?, version = ? WHERE folderId = ? AND folderImapUID = ?");
        query.bind(1, !remoteSeen);
        query.bind(2, remoteFlagged);
        query.bind(3, local.version + 1);
        query.bind(4, folder.id());
        query.bind(5, local.uid);
        query.exec();
    }
}

uint32_t MailStore::fetchMessageUIDAtDepth(Folder & folder, int depth) {
    SQLite::Statement query(this->_db, "SELECT folderImapUID FROM Message WHERE folderId = ? ORDER BY folderImapUID DESC LIMIT 1 OFFSET ?");
    query.bind(1, folder.id());
    query.bind(2, depth);
    if (query.executeStep()) {
        return query.getColumn("folderImapUID").getUInt();
    }
    return 1;
}


std::map<uint32_t, Message *> MailStore::fetchMessagesWithUIDs(std::vector<uint32_t> & uids, Folder & folder) {
    std::map<uint32_t, Message *> results;
    if (uids.size() == 0) {
        return results;
    }
    
    std::string qmarks{"?"};
    for (int i = 1; i < uids.size(); i ++) {
        qmarks = qmarks + ",?";
    }
    SQLite::Statement query(this->_db, "SELECT * FROM Message WHERE folderId = ? AND folderImapUID IN (" + qmarks + ")");
    int i = 1;
    query.bind(i++, folder.id());
    for(auto const &uid : uids) {
        query.bind(i++, uid);
    }
    while (query.executeStep()) {
        results[query.getColumn("folderImapUID").getUInt()] = new Message(query);
    }
    return results;
}


void MailStore::deleteMessagesWithUIDs(std::vector<uint32_t> & uids, Folder & folder) {
    if (uids.size() == 0) {
        return;
    }

    std::string qmarks{"?"};
    for (int i = 1; i < uids.size(); i ++) {
        qmarks = qmarks + ",?";
    }
    SQLite::Statement query(this->_db, "DELETE FROM Message WHERE folderId = ? AND folderImapUID IN (" + qmarks + ")");
    int i = 1;
    query.bind(i++, folder.id());
    for(auto const &uid : uids) {
        query.bind(i++, uid);
    }
    query.exec();
}

void MailStore::save(MailModel * model) {
    model->incrementVersion();
    
    if (model->version() > 1) {
        std::string pairs{""};
        for (const auto col : model->columnsForQuery()) {
            if (col == "id") {
                continue;
            }
            pairs += (col + " = :" + col + ",");
        }
        pairs.pop_back();
        
        SQLite::Statement query(this->_db, "UPDATE " + model->tableName() + " SET " + pairs + " WHERE id = :id");
        model->bindToQuery(query);
        query.exec();

    } else {
        std::string cols{""};
        std::string values{""};
        for (const auto col : model->columnsForQuery()) {
            cols += col + ",";
            values += ":" + col + ",";
        }
        cols.pop_back();
        values.pop_back();

        SQLite::Statement query(this->_db, "INSERT INTO " + model->tableName() + " (" + cols + ") VALUES (" + values + ")");
        model->bindToQuery(query);
        query.exec();
    }

    for (auto & observer : this->_observers) {
        observer->didPersistModel(model);
    }
}

void MailStore::remove(MailModel * model) {
    SQLite::Statement query(this->_db, "DELETE FROM " + model->tableName() + " WHERE id = :id LIMIT 1");
    query.bind(":id", model->id());
    query.exec();

    for (auto & observer : this->_observers) {
        observer->didUnpersistModel(model);
    }
}

#pragma mark Events

void MailStore::addObserver(MailStoreObserver * observer) {
    this->_observers.push_back(observer);
}
