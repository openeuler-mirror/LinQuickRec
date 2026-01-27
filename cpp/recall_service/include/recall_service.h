#ifndef LINQUICKREC_RECALL_SERVICE_H
#define LINQUICKREC_RECALL_SERVICE_H

#include "recall_service.pb.h"

namespace LinQuickRec {

    class RecallServiceImpl : public RecallService {
    public:
        RecallServiceImpl();
        ~RecallServiceImpl() override = default;
        void KRRecall(google::protobuf::RpcController *cntl_base,
                           const KRRecallRequest *req,
                           KRRecallResponse *resp,
                           google::protobuf::Closure *done) override;
    };

}

#endif //LINQUICKREC_RECALL_SERVICE_H
