/*
** Copyright (C) 2009-2015 Quadrant Information Security <quadrantsec.com>
** Copyright (C) 2009-2015 Champ Clark III <cclark@quadrantsec.com>
**
** This program is free software; you can redistribute it and/or modify
** it under the terms of the GNU General Public License Version 2 as
** published by the Free Software Foundation.  You may not use, modify or
** distribute this program under any other version of the GNU General
** Public License.
**
** This program is distributed in the hope that it will be useful,
** but WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
** GNU General Public License for more details.
**
** You should have received a copy of the GNU General Public License
** along with this program; if not, write to the Free Software
** Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
*/

/* sagan-blacklist.c
*
* This searches log lines for IP addresses/networks that are loaded
* from a "blacklist" file.  For example,  you might search log lines for
* known bad IP/Networks.  This processor uses the CIDR format:
* 192.168.1.1/32 (single ip) or 192.168.1.0./24.
*
*/

#ifdef HAVE_CONFIG_H
#include "config.h"             /* From autoconf */
#endif

#include <stdio.h>
#include <string.h>
#include <math.h>
#include <pthread.h>
#include <errno.h>
#include <stdbool.h>

#include "sagan.h"
#include "sagan-defs.h"
#include "sagan-blacklist.h"
#include "sagan-config.h"

#include "parsers/parsers.h"

struct _SaganCounters *counters;
struct _SaganConfig *config;
struct _SaganDebug *debug;
struct _Sagan_Blacklist *SaganBlacklist;

pthread_mutex_t SaganProcBlacklistWorkMutex=PTHREAD_MUTEX_INITIALIZER;

/****************************************************************************
 * Sagan_Blacklist_Init - Init any global memory structures we might need
 ****************************************************************************/

void Sagan_Blacklist_Init ( void )
{

    counters->blacklist_count=0;

    SaganBlacklist = malloc(sizeof(_Sagan_Blacklist));
    memset(SaganBlacklist, 0, sizeof(_Sagan_Blacklist));

}

/****************************************************************************
 * Sagan_Blacklist_Load - Loads 32 bit IP addresses into memory so that they
 * can be queried later
 ****************************************************************************/

void Sagan_Blacklist_Load ( void )
{

    FILE *blacklist;
    char *tok=NULL;
    char *tmpmask=NULL;
    int mask=0;
    char tmp[1024] = { 0 };
    char *iprange=NULL;
    char blacklistbuf[1024] = { 0 };
    char *blacklist_filename = NULL;
    char *ptmp = NULL;

    uint32_t u32_lower;
    uint32_t u32_higher;

    int line_count;
    int i;

    sbool found = 0;

    pthread_mutex_lock(&SaganProcBlacklistWorkMutex);
    counters->blacklist_count=0;
    pthread_mutex_unlock(&SaganProcBlacklistWorkMutex);

    blacklist_filename = strtok_r(config->blacklist_files, ",", &ptmp);

    while ( blacklist_filename != NULL )
        {

            Sagan_Log(S_NORMAL, "Blacklist Processor Loading File: %s.", blacklist_filename);


            if (( blacklist = fopen(blacklist_filename, "r" )) == NULL )
                {
                    Sagan_Log(S_ERROR, "[%s, line %d] Could not load blacklist file! (%s - %s)", __FILE__, __LINE__, blacklist_filename, strerror(errno));
                }


            line_count = 0;

            while(fgets(blacklistbuf, 1024, blacklist) != NULL)
                {

                    /* Skip comments and blank linkes */

                    if (blacklistbuf[0] == '#' || blacklistbuf[0] == 10 || blacklistbuf[0] == ';' || blacklistbuf[0] == 32)
                        {
                            line_count++;
                            continue;

                        }
                    else
                        {

                            /* Allocate memory for Blacklists,  not comments */

                            line_count++;

                            SaganBlacklist = (_Sagan_Blacklist *) realloc(SaganBlacklist, (counters->blacklist_count+1) * sizeof(_Sagan_Blacklist));

                            Remove_Return(blacklistbuf);

                            iprange = NULL;
                            tmpmask = NULL;

                            iprange = strtok_r(blacklistbuf, "/", &tok);
                            tmpmask = strtok_r(NULL, "/", &tok);

                            if ( tmpmask == NULL )
                                {

                                    /* If there is no CIDR,  then assume it's a /32 */

                                    strlcpy(tmp, iprange, sizeof(tmp));
                                    iprange = tmp;
                                    mask = 32;
                                }
                            else
                                {
                                    mask = atoi(tmpmask);
                                }

                            /* Should do better error checking? */

                            found = 0;

                            if ( iprange == NULL )
                                {

                                    Sagan_Log(S_ERROR, "[%s, line %d] Invalid range in %s at line %d, skipping....", __FILE__, __LINE__, blacklist_filename, line_count);
                                    found = 1;
                                }

                            if ( mask == 0 )
                                {

                                    Sagan_Log(S_ERROR, "[%s, line %d] Invalid mask in %s at line %d, skipping....", __FILE__, __LINE__, blacklist_filename, line_count);
                                    found = 1;

                                }

                            /* Record lower and upper range based on the /CIDR.  We then use IP2Bit(ipaddr) to determine
                             * if it's within the blacklist range.
                             *
                             * Idea came from "ashitpro"
                             * http://bytes.com/topic/c/answers/765104-determining-whether-given-ip-exist-cidr-ip-range
                             *
                             */


                            if ( found == 0 )
                                {

                                    u32_lower = IP2Bit(iprange);
                                    u32_higher = u32_lower + (pow(2,32-mask)-1);

                                    for ( i = 0; i < counters->blacklist_count; i++ )
                                        {

                                            if ( SaganBlacklist[i].u32_lower == u32_lower && SaganBlacklist[i].u32_higher == u32_higher )
                                                {
                                                    Sagan_Log(S_WARN, "[%s, line %d] Got duplicate blacklist address %s/%s in %s on line %d, skipping....", __FILE__, __LINE__, iprange, tmpmask, blacklist_filename, line_count);
                                                    found = 1;
                                                }
                                        }
                                }

                            if ( found == 0 )
                                {

                                    SaganBlacklist[counters->blacklist_count].u32_lower = u32_lower;
                                    SaganBlacklist[counters->blacklist_count].u32_higher = u32_higher;

                                    pthread_mutex_lock(&SaganProcBlacklistWorkMutex);
                                    counters->blacklist_count++;
                                    pthread_mutex_unlock(&SaganProcBlacklistWorkMutex);

                                }
                        }
                }

            fclose(blacklist);
            blacklist_filename = strtok_r(NULL, ",", &ptmp);

        }

}


/***************************************************************************
 * Sagan_Blacklist_IPADDR - Looks up the 32 bit IP address in the Blacklist
 * array.  If found,  returns TRUE.
 ***************************************************************************/

sbool Sagan_Blacklist_IPADDR ( uint32_t u32_ipaddr )
{

    int i;

    for ( i = 0; i < counters->blacklist_count; i++)
        {

            if ( ( u32_ipaddr > SaganBlacklist[i].u32_lower && u32_ipaddr < SaganBlacklist[i].u32_higher ) || ( u32_ipaddr == SaganBlacklist[i].u32_lower ) )
                {

                    pthread_mutex_lock(&SaganProcBlacklistWorkMutex);
                    counters->blacklist_hit_count++;
                    pthread_mutex_unlock(&SaganProcBlacklistWorkMutex);

                    return(true);
                }
        }

    return(false);

}

/***************************************************************************
 * Sagan_Blacklist_IPADDR_All - Check all IPv4 addresses against the
 * blacklist IP's in memory!
 ***************************************************************************/

sbool Sagan_Blacklist_IPADDR_All ( char *syslog_message )
{

    int i;
    int b;

    uint32_t ip;

    char *results = NULL;

    for (i = 1; i < MAX_PARSE_IP; i++)
        {

            results = parse_ip(syslog_message, i);

            /* Failed to find next IP,  short circuit the process */

            if (!strcmp(results, "0"))
                {
                    return(false);
                }

            ip = IP2Bit(results);

            for ( b = 0; b < counters->blacklist_count; b++ )
                {
                    if ( ( ip > SaganBlacklist[b].u32_lower && ip < SaganBlacklist[b].u32_higher ) || ( ip == SaganBlacklist[b].u32_lower ) )

                        {

                            pthread_mutex_lock(&SaganProcBlacklistWorkMutex);
                            counters->blacklist_hit_count++;
                            pthread_mutex_unlock(&SaganProcBlacklistWorkMutex);

                            return(true);
                        }
                }

        }

    return(false);
}

