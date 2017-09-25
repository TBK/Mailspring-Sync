//
//  DeltaStream.hpp
//  MailSync
//
//  Created by Ben Gotow on 6/15/17.
//  Copyright © 2017 Foundry 376. All rights reserved.
//

#ifndef DeltaStream_hpp
#define DeltaStream_hpp

#include <stdio.h>
#include <mutex>
#include <condition_variable>
#include "MailModel.hpp"
#include "json.hpp"
#include "spdlog/spdlog.h"

using namespace nlohmann;
using namespace std;

#define DELTA_TYPE_METADATA_EXPIRATION  "metadata-expiration"
#define DELTA_TYPE_PERSIST              "persist"
#define DELTA_TYPE_UNPERSIST            "unpersist"

class DeltaStreamItem {
public:
    string type;
    vector<json> modelJSONs;
    string modelClass;
    
    DeltaStreamItem(string type, string modelClass, vector<json> modelJSONs);
    DeltaStreamItem(string type, vector<shared_ptr<MailModel>> & models);
    DeltaStreamItem(string type, MailModel * model);
    
    bool concatenate(const DeltaStreamItem & other);
    void upsertModelJSON(const json & modelJSON);
    string dump() const;
};

class DeltaStream  {
    mutex bufferMtx;
    map<string, vector<DeltaStreamItem>> buffer;

    bool scheduled;
    std::chrono::system_clock::time_point scheduledTime;
    std::mutex bufferFlushMtx;
    std::condition_variable bufferFlushCv;

public:
    DeltaStream();
    ~DeltaStream();

    json waitForJSON();

    void flushBuffer();
    void flushWithin(int ms);
    
    void queueDeltaForDelivery(DeltaStreamItem item);

    void emit(DeltaStreamItem item, int maxDeliveryDelay);
    void emit(vector<DeltaStreamItem> items, int maxDeliveryDelay);
};


shared_ptr<DeltaStream> SharedDeltaStream();

#endif /* DeltaStream_hpp */
