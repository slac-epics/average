/* averageRecord.c - Record Support Routines for Average record */
/*
 *      Author: Dehong Zhang / SLAC
 *      Date:   June, 2011
 *
 */

#include <stddef.h>
#include <stdlib.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "dbDefs.h"
#include "alarm.h"
#include "dbAccess.h"
#include "dbEvent.h"
#include "devSup.h"
#include "errMdef.h"
#include "special.h"
#include "recSup.h"
#include "recGbl.h"
#define GEN_SIZE_OFFSET
#include "averageRecord.h"
#undef  GEN_SIZE_OFFSET
#include "epicsExport.h"

/* Create RSET - Record Support Entry Table*/
#define report            NULL
#define initialize        NULL

static long init_record(void *precord, int pass);
static long process(void *precord);
static long cvt_dbaddr(dbAddr *paddr);

static void monitor(void *precord);

#define special            NULL

#define get_value          NULL
#define get_array_info     NULL
#define put_array_info     NULL
#define get_units          NULL
#define get_precision      NULL
#define get_enum_str       NULL
#define get_enum_strs      NULL
#define put_enum_str       NULL
#define get_graphic_double NULL
#define get_control_double NULL
#define get_alarm_double   NULL

rset averageRSET={
	RSETNUMBER,
	report,
	initialize,
	(RECSUPFUN) init_record,
	(RECSUPFUN) process,
	special,
	get_value,
	(RECSUPFUN) cvt_dbaddr,
	get_array_info,
	put_array_info,
	get_units,
	get_precision,
	get_enum_str,
	get_enum_strs,
	put_enum_str,
	get_graphic_double,
	get_control_double,
	get_alarm_double };

epicsExportAddress(rset,averageRSET);


static void checkAlarms(void *precord);

static long init_record(void *precord, int pass)
{
    averageRecord *pAvg = (averageRecord *)precord;

    if ( pass == 0 )
    {
        if ( pAvg->navg < 1 ) pAvg->navg = 1;

        pAvg->idx  = -1;
        pAvg->sum  =  0.;
        pAvg->val  =  0.;
        pAvg->dbuf =  (double *)calloc( pAvg->navg, sizeof(double) );
    }

    return(0);
}

static long process(void *precord)
{
    averageRecord *pAvg = (averageRecord *)precord;

    pAvg->pact = TRUE;

    pAvg->idx++;
    if ( pAvg->idx == pAvg->navg ) pAvg->idx = 0;

    pAvg->sum = pAvg->sum + pAvg->inp - pAvg->dbuf[ pAvg->idx ];
    pAvg->val = pAvg->sum / pAvg->navg;

    pAvg->dbuf[ pAvg->idx ] = pAvg->inp;

    recGblGetTimeStamp( pAvg );

    /* check for alarms */
    checkAlarms( pAvg );

    /* check the event list */
    monitor( pAvg );

    /* process the forward scan link record */
    recGblFwdLink( pAvg );

    pAvg->udf  = FALSE;
    pAvg->pact = FALSE;
    return( 0 );
}

static long cvt_dbaddr(dbAddr *paddr)
{
    averageRecord *pAvg = (averageRecord *)paddr->precord;
    int            fieldIndex, status = 0;

    fieldIndex = dbGetFieldIndex( paddr );

    switch (fieldIndex)
    {
        case averageRecordDBUF:
        {
            paddr->pfield         = (double *) pAvg->dbuf;
            paddr->no_elements    = pAvg->navg;
            paddr->field_type     = DBF_DOUBLE;
            paddr->field_size     = sizeof(double);
            paddr->dbr_field_type = DBR_DOUBLE;
            break;
        }
    }

    return( status );
}

static void checkAlarms(void *precord)
{
    averageRecord *pAvg = (averageRecord *)precord;

    double		val;
    double		hyst, lalm, hihi, high, low, lolo;
    unsigned short	hhsv, llsv, hsv, lsv;

    if ( pAvg->udf )
    {
        recGblSetSevr( pAvg, UDF_ALARM, INVALID_ALARM );
        return;
    }

    hihi = pAvg->hihi;
    lolo = pAvg->lolo;
    high = pAvg->high;
    low  = pAvg->low;

    hhsv = pAvg->hhsv;
    llsv = pAvg->llsv;
    hsv  = pAvg->hsv;
    lsv  = pAvg->lsv;

    val  = pAvg->val;
    hyst = pAvg->hyst;
    lalm = pAvg->lalm;

    /* alarm condition hihi */
    if ( hhsv && (val >= hihi || ((lalm==hihi) && (val >= hihi-hyst))) )
    {
        if ( recGblSetSevr(pAvg, HIHI_ALARM, pAvg->hhsv) ) pAvg->lalm = hihi;
        return;
    }

    /* alarm condition lolo */
    if ( llsv && (val <= lolo || ((lalm==lolo) && (val <= lolo+hyst))) )
    {
        if ( recGblSetSevr(pAvg, LOLO_ALARM, pAvg->llsv) ) pAvg->lalm = lolo;
        return;
    }

    /* alarm condition high */
    if ( hsv  && (val >= high || ((lalm==high) && (val >= high-hyst))) )
    {
        if ( recGblSetSevr(pAvg, HIGH_ALARM, pAvg->hsv ) ) pAvg->lalm = high;
       	return;
    }

    /* alarm condition low */
    if ( lsv  && (val <= low  || ((lalm==low ) && (val <= low +hyst))) )
    {
        if ( recGblSetSevr(pAvg, LOW_ALARM,  pAvg->lsv ) ) pAvg->lalm = low;
        return;
    }

    /* we get here only if val is out of alarm by at least hyst */
    pAvg->lalm = val;
    return;
}

static void monitor(void *precord)
{
    averageRecord  *pAvg = (averageRecord *)precord;
    unsigned short  monitor_mask;
    double          delta;

    monitor_mask = recGblResetAlarms( pAvg );

    /* check for value change */
    delta = pAvg->mlst - pAvg->val;
    if ( delta < 0.0 ) delta = -delta;

    if ( delta > pAvg->mdel )
    {
        /* post events for value change */
        monitor_mask |= DBE_VALUE;
        /* update last value monitored */
        pAvg->mlst = pAvg->val;
    }

    /* check for archive change */
    delta = pAvg->alst - pAvg->val;
    if ( delta < 0.0 ) delta = -delta;

    if ( delta > pAvg->adel )
    {
        /* post events for archive change */
        monitor_mask |= DBE_LOG;
        /* update last archive value monitored */
        pAvg->alst = pAvg->val;
    }

    /* send out monitors connected to the value field */
    if ( monitor_mask ) db_post_events( pAvg, &pAvg->val, monitor_mask );

    return;
}

