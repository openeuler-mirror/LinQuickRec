#include "precalc_server.h"
#include "rank_master_server.h"

#include <brpc/server.h>
#include <gflags/gflags.h>
#include "common/logger.h"

DEFINE_int32(discovery_refresh_interval_ms, 5000, "");
DEFINE_int32(server_num_threads, 0, "");
DEFINE_int32(server_idle_timeout_sec, -1, "");
DEFINE_int32(server_max_concurrency, 0, "");

static void configure_opts(brpc::ServerOptions& opts) {
    if (FLAGS_server_num_threads > 0) opts.num_threads = FLAGS_server_num_threads;
    if (FLAGS_server_idle_timeout_sec >= 0) opts.idle_timeout_sec = FLAGS_server_idle_timeout_sec;
    if (FLAGS_server_max_concurrency > 0) opts.max_concurrency = FLAGS_server_max_concurrency;
}

int main(int argc, char* argv[]) {
    gflags::ParseCommandLineFlags(&argc, &argv, true);

    common::logger::LoggerConfig cfg;
    cfg.level = common::logger::LogLevel::INFO;
    cfg.console_output = true;
    cfg.file_path = "/var/log/linquickrec/precalc_and_rank_master.log";
    cfg.max_file_size = 100 * 1024 * 1024;
    cfg.max_files = 5;
    cfg.enable_trace_id = true;
    common::logger::Initialize(cfg);

    precalc::PrecalcServiceImpl precalc_svc;
    rank::RankMasterServiceImpl rank_master_svc;

    brpc::Server precalc_svr, rank_master_svr;
    int ret = precalc_svr.AddService(&precalc_svc, brpc::SERVER_DOESNT_OWN_SERVICE);
    assert(ret == 0);
    ret = rank_master_svr.AddService(&rank_master_svc, brpc::SERVER_DOESNT_OWN_SERVICE);
    assert(ret == 0);

    brpc::ServerOptions precalc_opts;
    configure_opts(precalc_opts);
    ret = precalc_svr.Start(FLAGS_server_port, &precalc_opts);
    assert(ret == 0);
    LOG_INFO << "PrecalcService started on port " << FLAGS_server_port;

    brpc::ServerOptions rank_master_opts;
    configure_opts(rank_master_opts);
    ret = rank_master_svr.Start(FLAGS_rank_master_server_port, &rank_master_opts);
    assert(ret == 0);
    LOG_INFO << "RankMasterService started on port " << FLAGS_rank_master_server_port;

    precalc_svr.RunUntilAskedToQuit();
    rank_master_svr.Stop(0); rank_master_svr.Join();
    LOG_INFO << "Precalc-and-Rank-Master stopped";
    return 0;
}
