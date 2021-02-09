/** ProgressCollectors [MailSync]
 *
 * Author(s): Ben Gotow
 */

/* LICENSE
* Copyright (C) 2017-2021 Foundry 376.
*
* This program is free software: you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation, either version 3 of the License, or
* (at your option) any later version.
*
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License
* along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

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