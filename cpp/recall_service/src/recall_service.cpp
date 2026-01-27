#include "recall_service.h"

namespace LinQuickRec {
        RecallServiceImpl::RecallServiceImpl(){}

        void RecallServiceImpl::KRRecall(google::protobuf::RpcController *cntl_base,
                      const KRRecallRequest *req,
                      KRRecallResponse *resp,
                      google::protobuf::Closure *done) {
            //TODO: implement KRRecall based on Qwen
        }

}