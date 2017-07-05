//
//  ProgressCollectors.hpp
//  MailSync
//
//  Created by Ben Gotow on 7/5/17.
//  Copyright © 2017 Foundry 376. All rights reserved.
//

#ifndef ProgressCollectors_hpp
#define ProgressCollectors_hpp

#include <stdio.h>
#include <MailCore/MailCore.h>

using namespace mailcore;

class IMAPProgress : public IMAPProgressCallback {
public:
    void bodyProgress(IMAPSession * session, unsigned int current, unsigned int maximum);
    void itemsProgress(IMAPSession * session, unsigned int current, unsigned int maximum);
};

class SMTPProgress : public SMTPProgressCallback {
public:
    
    void bodyProgress(IMAPSession * session, unsigned int current, unsigned int maximum);
};


#endif /* ProgressCollectors_hpp */
