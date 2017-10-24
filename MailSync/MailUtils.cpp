//
//  MailUtils.cpp
//  MailSync
//
//  Created by Ben Gotow on 6/15/17.
//  Copyright © 2017 Foundry 376. All rights reserved.
//

#include "MailUtils.hpp"
#include "SyncException.hpp"
#include "sha256.h"
#include "constants.h"
#include "File.hpp"
#include "Label.hpp"
#include "Account.hpp"
#include "Query.hpp"

#if defined(_MSC_VER)
#include <direct.h>
#endif

using namespace std;
using namespace mailcore;
using namespace nlohmann;

static vector<string> unworthyPrefixes = {
    "noreply",
    "no-reply",
    "no_reply",
    "auto-confirm",
    "donotreply",
    "do-not-reply",
    "do_not_reply",
    "auto-reply",
    "inmail-hit-reply",
    "updates@",
    "mailman-owner",
    "email_notifier",
    "announcement",
    "bounce",
    "notification",
    "notify@",
    "support",
    "alert",
    "news",
    "info",
    "automated",
    "list",
    "distribute",
    "catchall",
    "catch-all"
};

static bool calledsrand = false;

bool create_directory(string dir) {
    int c = 0;
#if defined(_WIN32)
    c = _mkdir(dir.c_str());
#else
    c = mkdir(dir.c_str(), S_IRWXU | S_IRWXG | S_IRWXO);
#endif
    return true;
}

/** All alphanumeric characters except for "0", "I", "O", and "l" */
static const char* pszBase58 = "123456789ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz";

// From https://github.com/bitcoin/bitcoin/blob/master/src/base58.cpp
std::string MailUtils::toBase58(const unsigned char * pbegin, size_t len)
{
    const unsigned char * pend = pbegin + len;

    // Skip & count leading zeroes.
    int zeroes = 0;
    int length = 0;
    while (pbegin != pend && *pbegin == 0) {
        pbegin++;
        zeroes++;
    }
    // Allocate enough space in big-endian base58 representation.
    long size = (pend - pbegin) * 138 / 100 + 1; // log(256) / log(58), rounded up.
    std::vector<unsigned char> b58(size);
    // Process the bytes.
    while (pbegin != pend) {
        int carry = *pbegin;
        int i = 0;
        // Apply "b58 = b58 * 256 + ch".
        for (std::vector<unsigned char>::reverse_iterator it = b58.rbegin(); (carry != 0 || i < length) && (it != b58.rend()); it++, i++) {
            carry += 256 * (*it);
            *it = carry % 58;
            carry /= 58;
        }
        
        assert(carry == 0);
        length = i;
        pbegin++;
    }
    // Skip leading zeroes in base58 result.
    std::vector<unsigned char>::iterator it = b58.begin() + (size - length);
    while (it != b58.end() && *it == 0)
        it++;
    // Translate the result into a string.
    std::string str;
    str.reserve(zeroes + (b58.end() - it));
    str.assign(zeroes, '1');
    while (it != b58.end())
        str += pszBase58[*(it++)];
    return str;
}


json MailUtils::merge(const json &a, const json &b)
{
    json result = a.flatten();
    json tmp = b.flatten();
    
    for (json::iterator it = tmp.begin(); it != tmp.end(); ++it)
    {
        result[it.key()] = it.value();
    }
    
    return result.unflatten();
}

json MailUtils::contactJSONFromAddress(Address * addr) {
    json contact;
    // note: for some reason, using ternarys here doesn't work.
    if (addr->displayName()) {
        contact["name"] = addr->displayName()->UTF8Characters();
    }
    if (addr->mailbox()) {
        contact["email"] = addr->mailbox()->UTF8Characters();
    }
    return contact;
}

Address * MailUtils::addressFromContactJSON(json & j) {
    if (j["name"].is_string()) {
        return Address::addressWithDisplayName(AS_MCSTR(j["name"].get<string>()), AS_MCSTR(j["email"].get<string>()));
    }
    return Address::addressWithMailbox(AS_MCSTR(j["email"].get<string>()));
}

string MailUtils::contactKeyForEmail(string email) {
    
    // lowercase the email
    transform(email.begin(), email.end(), email.begin(), ::tolower);

    // check for anything longer then X - likely autogenerated and not a contact
    if (email.length() > 40) {
        return "";
    }
    // fast check for common prefixes we don't want
    for (string & prefix : unworthyPrefixes) {
        if (equal(email.begin(), email.begin() + min(email.size(), prefix.size()), prefix.begin())) {
            return "";
        }
    }
    
    // check for non-prefix scenarios
    if (email.find("@noreply") != string::npos) { // x@noreply.github.com
        return "";
    }
    if (email.find("@notifications") != string::npos) { // x@notifications.intuit.com
        return "";
    }
    if (email.find("noreply@") != string::npos) { // reservations-noreply@bla.com
        return "";
    }
    
    return email;
}

int MailUtils::compareEmails(void * a, void * b, void * context) {
    return ((String*)a)->compare((String*)b);
}

string MailUtils::timestampForTime(time_t time) {
	// Some messages can have date=-1 if no Date: header is present. Win32
	// doesn't allow this value, so we always convert it to one second past 1970.
	if (time == -1) {
		time = 1;
	}

	char buffer[32];
#if defined(_MSC_VER)
	tm ptm;
	localtime_s(&ptm, &time);
	strftime(buffer, 32, "%Y-%m-%d %H:%M:%S", &ptm);
#else
    tm * ptm = localtime(&time);
	strftime(buffer, 32, "%Y-%m-%d %H:%M:%S", ptm);
#endif
    return string(buffer);
}

string MailUtils::namespacePrefixOrBlank(IMAPSession * session) {
    if (!session->defaultNamespace()->mainPrefix()) {
        return "";
    }
    return session->defaultNamespace()->mainPrefix()->UTF8Characters();
}

vector<string> MailUtils::roles() {
    return {"all", "sent", "drafts", "spam", "important", "starred", "archive", "inbox", "trash", "snoozed"};
}

string MailUtils::roleForFolder(string mainPrefix, IMAPFolder * folder) {
    string role = MailUtils::roleForFolderViaFlags(mainPrefix, folder);
    if (role == "") {
        role = MailUtils::roleForFolderViaPath(mainPrefix, folder);
    }
    return role;
}

string MailUtils::roleForFolderViaFlags(string mainPrefix, IMAPFolder * folder) {
    IMAPFolderFlag flags = folder->flags();
    
    if (flags & IMAPFolderFlagAll) {
        return "all";
    }
    if (flags & IMAPFolderFlagSentMail) {
        return "sent";
    }
    if (flags & IMAPFolderFlagDrafts) {
        return "drafts";
    }
    if (flags & IMAPFolderFlagJunk) {
        return "spam";
    }
    if (flags & IMAPFolderFlagSpam) {
        return "spam";
    }
    if (flags & IMAPFolderFlagImportant) {
        return "important";
    }
    if (flags & IMAPFolderFlagStarred) {
        return "starred";
    }
    if (flags & IMAPFolderFlagInbox) {
        return "inbox";
    }
    if (flags & IMAPFolderFlagTrash) {
        return "trash";
    }
    return "";
}

string MailUtils::roleForFolderViaPath(string mainPrefix, IMAPFolder * folder) {
    string delimiter {folder->delimiter()};
    string path = string(folder->path()->UTF8Characters());

    // Strip the namespace prefix if it's present
    if ((mainPrefix.size() > 0) && (path.size() > mainPrefix.size()) && (path.substr(0, mainPrefix.size()) == mainPrefix)) {
        path = path.substr(mainPrefix.size());
    }

    // Lowercase the path
    transform(path.begin(), path.end(), path.begin(), ::tolower);

    // In our [Mailspring] subfolder, folder names are roles:
    // [mailspring]/snoozed = snoozed
    // [mailspring]/XXX = xxx
    string mailspring = MAILSPRING_FOLDER_PREFIX;
    transform(mailspring.begin(), mailspring.end(), mailspring.begin(), ::tolower);
    if (path.size() > mailspring.size() && path.substr(0, mailspring.size()) == mailspring) {
        return path.substr(mailspring.size() + 1);
    }
    
    // Match against a lookup table of common names
    // [Gmail]/Spam => [gmail]/spam => spam
    if (COMMON_FOLDER_NAMES.find(path) != COMMON_FOLDER_NAMES.end()) {
        return COMMON_FOLDER_NAMES[path];
    }

    return "";
}

string MailUtils::pathForFile(string root, File * file, bool create) {
    string id = file->id();
    transform(id.begin(), id.end(), id.begin(), ::tolower);
    
    if (create && !create_directory(root)) { return ""; }
    string path = root + FS_PATH_SEP + id.substr(0, 2);
    if (create && !create_directory(path)) { return ""; }
    path += FS_PATH_SEP + id.substr(2, 2);
    if (create && !create_directory(path)) { return ""; }
    path += FS_PATH_SEP + id;
    if (create && !create_directory(path)) { return ""; }
    
    path += FS_PATH_SEP + file->safeFilename();
    return path;
}

shared_ptr<Label> MailUtils::labelForXGMLabelName(string mlname, vector<shared_ptr<Label>> allLabels) {
    for (const auto & label : allLabels) {
        if (label->path() == mlname) {
            return label;
        }
    }
    
    // \\Inbox should match INBOX
    if (mlname.substr(0, 1) == "\\") {
        mlname = mlname.substr(1, mlname.length() - 1);
        transform(mlname.begin(), mlname.end(), mlname.begin(), ::tolower);

        for (const auto & label : allLabels) {
            string path(label->path());
            transform(path.begin(), path.end(), path.begin(), ::tolower);
            if (path.substr(0, 8) == "[gmail]/") {
                path = path.substr(8, path.length() - 8);
            }
                
            if (path == mlname) {
                return label;
            }
            
            // sent => [Gmail]/Sent Mail (sent), draft => [Gmail]/Drafts (drafts)
            if ((label->role() == mlname) || (label->role() == mlname + "s")) {
                return label;
            }
        }
    }

    cout << "\n\nIMPORTANT --- Label not found: " << mlname;
    return shared_ptr<Label>{};
}

vector<Query> MailUtils::queriesForUIDRangesInIndexSet(string remoteFolderId, IndexSet * set) {
    vector<Query> results {};
    vector<uint32_t> uids {};
    
    Range * range = set->allRanges();
    for (int ii = 0; ii < set->rangesCount(); ii++) {
        if (range->length == UINT64_MAX) {
            // this range has a * upper bound, we need to represent it as a "uid > X" query.
            results.push_back(Query().equal("remoteFolderId", remoteFolderId).gte("remoteUID", range->location));
        } else {
            // this range is a set of UIDs. Add them to a big buffer and we'll break them into
            // a small number of queries below.
            for (int x = 0; x < range->length; x ++) {
                uids.push_back((uint32_t)(range->location + x));
            }
        }
        range += sizeof(Range *);
    }

    for (vector<uint32_t> chunk : MailUtils::chunksOfVector(uids, 200)) {
        results.push_back(Query().equal("remoteFolderId", remoteFolderId).equal("remoteUID", chunk));
    }

    return results;
}

vector<uint32_t> MailUtils::uidsOfArray(Array * array) {
    vector<uint32_t> uids {};
    uids.reserve(array->count());
    for (int ii = 0; ii < array->count(); ii++) {
        uids.push_back(((IMAPMessage*)array->objectAtIndex(ii))->uid());
    }
    return uids;
}

string MailUtils::idForFolder(string accountId, string folderPath) {
    vector<unsigned char> hash(32);
    string src_str = accountId + ":" + folderPath;
    picosha2::hash256(src_str.begin(), src_str.end(), hash.begin(), hash.end());
    return toBase58(hash.data(), 30);
}

string MailUtils::idForFile(Message * message, Attachment * attachment) {
    vector<unsigned char> hash(32);
    string src_str = message->id() + ":" + message->accountId() + ":" + attachment->partID()->UTF8Characters() + ":" + attachment->uniqueID()->UTF8Characters();
    picosha2::hash256(src_str.begin(), src_str.end(), hash.begin(), hash.end());
    return toBase58(hash.data(), 30);
}

string MailUtils::idRandomlyGenerated() {
    static string charset = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ1234567890";
    string result;
    result.resize(40);
    
    if (!calledsrand) {
        srand((unsigned int)time(0));
        calledsrand = true;
    }
    for (int i = 0; i < 40; i++) {
        result[i] = charset[rand() % charset.length()];
    }
    return result;
}

string MailUtils::idForDraftHeaderMessageId(string accountId, string headerMessageId)
{
    vector<unsigned char> hash(32);
    string src_str = accountId + ":" + headerMessageId;
    picosha2::hash256(src_str.begin(), src_str.end(), hash.begin(), hash.end());
    return toBase58(hash.data(), 30);
}

string MailUtils::idForMessage(string accountId, IMAPMessage * msg) {
    Array * addresses = new Array();
    addresses->addObjectsFromArray(msg->header()->to());
    addresses->addObjectsFromArray(msg->header()->cc());
    addresses->addObjectsFromArray(msg->header()->bcc());
    
    Array * emails = new Array();
    for (int i = 0; i < addresses->count(); i ++) {
        Address * addr = (Address*)addresses->objectAtIndex(i);
        emails->addObject(addr->mailbox());
    }
    
    emails->sortArray(compareEmails, NULL);
    
    String * participants = emails->componentsJoinedByString(MCSTR(""));

    addresses->release();
    emails->release();
    
    String * messageID = msg->header()->isMessageIDAutoGenerated() ? MCSTR("") : msg->header()->messageID();
    String * subject = msg->header()->subject();
    
    string src_str = accountId;
    src_str = src_str.append("-");
    src_str = src_str.append(timestampForTime(msg->header()->date()));
    if (subject) {
        src_str = src_str.append(subject->UTF8Characters());
    }
    src_str = src_str.append("-");
    src_str = src_str.append(participants->UTF8Characters());
    src_str = src_str.append("-");
    src_str = src_str.append(messageID->UTF8Characters());
    
    vector<unsigned char> hash(32);
    picosha2::hash256(src_str.begin(), src_str.end(), hash.begin(), hash.end());
    return toBase58(hash.data(), 30);
}

string MailUtils::qmarks(size_t count) {
    if (count == 0) {
        return "";
    }
    string qmarks{"?"};
    for (int i = 1; i < count; i ++) {
        qmarks = qmarks + ",?";
    }
    return qmarks;
}

string MailUtils::qmarkSets(size_t count, size_t perSet) {
    if (count == 0) {
        return "";
    }
    string one = "(" + MailUtils::qmarks(perSet) + ")";
    string qmarks{one};
    for (int i = 1; i < count; i ++) {
        qmarks = qmarks + "," + one;
    }
    return qmarks;
}

void MailUtils::configureSessionForAccount(IMAPSession & session, shared_ptr<Account> account) {
    if (account->refreshToken() != "") {
        XOAuth2Parts parts = SharedXOAuth2TokenManager()->partsForAccount(account);
        session.setUsername(AS_MCSTR(parts.username));
        session.setOAuth2Token(AS_MCSTR(parts.accessToken));
        session.setAuthType(AuthTypeXOAuth2);
    } else {
        session.setUsername(AS_MCSTR(account->IMAPUsername()));
        session.setPassword(AS_MCSTR(account->IMAPPassword()));
    }
    session.setHostname(AS_MCSTR(account->IMAPHost()));
    session.setPort(account->IMAPPort());
    if (account->IMAPSecurity() == "SSL / TLS") {
        session.setConnectionType(ConnectionType::ConnectionTypeTLS);
    } else if (account->IMAPSecurity() == "STARTTLS") {
        session.setConnectionType(ConnectionType::ConnectionTypeStartTLS);
    } else {
        session.setConnectionType(ConnectionType::ConnectionTypeClear);
    }
    if (account->IMAPAllowInsecureSSL()) {
        session.setCheckCertificateEnabled(false);
    }
}

void MailUtils::configureSessionForAccount(SMTPSession & session, shared_ptr<Account> account) {
    if (account->refreshToken() != "") {
        XOAuth2Parts parts = SharedXOAuth2TokenManager()->partsForAccount(account);
        session.setUsername(AS_MCSTR(parts.username));
        session.setOAuth2Token(AS_MCSTR(parts.accessToken));
        session.setAuthType(AuthTypeXOAuth2);
    } else {
        session.setUsername(AS_MCSTR(account->SMTPUsername()));
        session.setPassword(AS_MCSTR(account->SMTPPassword()));
    }
    session.setHostname(AS_MCSTR(account->SMTPHost()));
    session.setPort(account->SMTPPort());
    if (account->SMTPSecurity() == "SSL / TLS") {
        session.setConnectionType(ConnectionType::ConnectionTypeTLS);
    } else if (account->SMTPSecurity() == "STARTTLS") {
        session.setConnectionType(ConnectionType::ConnectionTypeStartTLS);
    } else {
        session.setConnectionType(ConnectionType::ConnectionTypeClear);
    }
    if (account->SMTPAllowInsecureSSL()) {
        session.setCheckCertificateEnabled(false);
    }
}

// Worker Sleep Implementation


std::mutex workerSleepMtx;
std::condition_variable workerSleepCV;

void MailUtils::sleepWorkerUntilWakeOrSec(int sec) {
    auto desiredTime = std::chrono::system_clock::now();
    desiredTime += chrono::milliseconds(sec * 1000);
    unique_lock<mutex> lck(workerSleepMtx);
    workerSleepCV.wait_until(lck, desiredTime);
}

void MailUtils::wakeAllWorkers() {
    lock_guard<mutex> lck(workerSleepMtx);
    workerSleepCV.notify_all();
}

