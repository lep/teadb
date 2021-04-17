#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sqlite3.h>
#include <libxo/xo.h>


void emit_drinking_log(sqlite3 *db, int limit){
    sqlite3_stmt *stmt;
    sqlite3_prepare_v2(db, "select tea, date from log order by date desc limit 10", -1, &stmt, NULL);

    xo_open_list("log");
    while( sqlite3_step(stmt) != SQLITE_DONE){
        xo_open_instance("log");
	char *tea = sqlite3_column_text(stmt, 0);
	char *date = sqlite3_column_text(stmt, 1);
        xo_emit("{:date}: {:tea}\n", date, tea);
        xo_close_instance("log");
    }
    xo_close_list("log");
    sqlite3_finalize(stmt);

}

void emit_global_stats(sqlite3 *db){
    sqlite3_stmt *amounts_per_type;

    char *query = " select A.type, A.amnt, B.amnt from"
		  " 	(select type, sum(amount) as amnt"
		  " 	 from teas group by type) as A"
		  " join (select type, sum(log.amount) as amnt"
		  "       from teas join log on log.tea = teas.name"
		  " 	  group by type) as B"
		  " on A.type = B.type";

    int r = sqlite3_prepare_v2(db, query, -1, &amounts_per_type, NULL);

    if( r != SQLITE_OK ){
	fprintf(stderr, "Error emit_drinking_log (%d)\n", r);
	exit(1);
    }

    xo_open_container("amount/type");
    while( sqlite3_step(amounts_per_type) != SQLITE_DONE){
	char *type = sqlite3_column_text(amounts_per_type, 0);
	double total_amount = sqlite3_column_double(amounts_per_type, 1);
	double drunk_amount = sqlite3_column_double(amounts_per_type, 2);
	xo_open_container(type);
	xo_emit("{:type}: {:drank/%.3f}g/{:total/%.3f}g\n", type, drunk_amount, total_amount);
	xo_close_container(type);
	//xo_emit_field("V", type, "Total amount of %s: %.3f\n", "%.3f", type, amount);
    }
    xo_close_container("amount/type");
    sqlite3_finalize(amounts_per_type);
}

void emit_tea_stats(sqlite3 *db, const char *tea){
    xo_open_container("detail");

    xo_emit("{:tea}\n", tea);

    sqlite3_stmt *stmt;
    sqlite3_prepare_v2(db, "select avg(amount) from log where tea = ?", -1, &stmt, NULL);
    sqlite3_bind_text(stmt, 1, tea, -1, NULL);

    sqlite3_step(stmt);

    double avg_amount = sqlite3_column_double(stmt, 0);
    xo_emit("Average session size: {:avg-amount/%.3f}g\n", avg_amount);
    sqlite3_finalize(stmt);


    sqlite3_prepare_v2(db, "select sum(amount), count(*) from teas where name = ?", -1, &stmt, NULL);
    sqlite3_bind_text(stmt, 1, tea, -1, NULL);
    sqlite3_step(stmt);

    double total_amount = sqlite3_column_double(stmt, 0);
    int num_orders = sqlite3_column_int(stmt, 1);

    xo_emit("Bought {:total-amount/%g}g in {:num-orders/%d} order(s)\n", total_amount, num_orders);
    sqlite3_finalize(stmt);

    sqlite3_prepare_v2(db, "select url, type, year from teas where name = ?", -1, &stmt, NULL);
    sqlite3_bind_text(stmt, 1, tea, -1, NULL);
    sqlite3_step(stmt);

    char *url = sqlite3_column_text(stmt, 0);
    char *type = sqlite3_column_text(stmt, 1);
    char *year = sqlite3_column_text(stmt, 2);

    xo_emit("URL: {:url}\nType: {:type}\nYear: {:year}\n", url, type, year);
    sqlite3_finalize(stmt);

    sqlite3_prepare_v2(db, "select sum(amount) from log where tea = ?", -1, &stmt, NULL);
    sqlite3_bind_text(stmt, 1, tea, -1, NULL);
    sqlite3_step(stmt);

    double amount_drank = sqlite3_column_double(stmt, 0);
    xo_emit("Drank {:amount-drank/%.3f}g so far\n", amount_drank);
    xo_emit("Roughly {:amount-left/%.3f}g left\n", total_amount - amount_drank);


    sqlite3_prepare_v2(db, "select date, uid from log where tea = ? order by date desc", -1, &stmt, NULL);
    sqlite3_bind_text(stmt, 1, tea, -1, NULL);

    xo_open_list("sessions");
    while( sqlite3_step(stmt) != SQLITE_DONE) {
	xo_open_instance("sessions");
	char *date = sqlite3_column_text(stmt, 0);
	char *uid = sqlite3_column_text(stmt, 1);
	xo_emit("Session on {:date} with uid {:uid}\n", date, uid);
	xo_close_instance("sessions");
    }
    xo_close_list("sessions");
    sqlite3_finalize(stmt);

    xo_close_container("detail");
}

void emit_log_entry(sqlite3 *db, const char *uid){
    xo_open_container("session");
    
    sqlite3_stmt *stmt;
    sqlite3_prepare_v2(db, "select tea, amount, date, notes from log where uid = ?", -1, &stmt, NULL);
    sqlite3_bind_text(stmt, 1, uid, -1, NULL);
    sqlite3_step(stmt);

    char *tea = sqlite3_column_text(stmt, 0);
    double amount = sqlite3_column_double(stmt, 1);
    char *date = sqlite3_column_text(stmt, 2);
    char *notes = sqlite3_column_text(stmt, 3);

    xo_emit("Drank {:amount/%.3f}g of {:tea} on {:date}\n", amount, tea, date);
    xo_emit("Notes:\n{:notes}\n", notes);

    emit_tea_stats(db, tea);

    sqlite3_finalize(stmt);

    xo_close_container("session");
}

int main(int argc, char **argv){
    argc = xo_parse_args(argc, argv);
    if(argc < 0)
	exit(1);

    xo_open_container("tea");
    if(argc < 3){
	printf("Usage: %s tea.db [global|tea|session] <tea name|session-id>\n", argv[0]);
	exit(1);
    }


    char *dbpath = argv[1];
    sqlite3 *db;
    if( sqlite3_open_v2(dbpath, &db, SQLITE_OPEN_READONLY, NULL) != SQLITE_OK ){
	fprintf(stderr, "Error while opening sqlite-file %s\n", dbpath);
	exit(1);
    }

    if( !strcmp(argv[2], "global") ){
	emit_drinking_log(db, 10);
	emit_global_stats(db);
    }else if( !strcmp(argv[2], "tea") ){
	if(argc != 4){
	    exit(1);
	}
	emit_tea_stats(db, argv[3]);
    }else if( !strcmp(argv[2], "session")){
	if(argc != 4){
	    exit(1);
	}
	emit_log_entry(db, argv[3]);
    }


    xo_close_container("tea");
    xo_finish();

    sqlite3_close(db);

}
