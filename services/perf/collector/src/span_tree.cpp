#include "span_tree.h"

#include <algorithm>

namespace perf {

// ---- SpanTreeTemplate ----

SpanTreeTemplate::SpanTreeTemplate() {
    InitDefaults();
}

SpanTreeTemplate& SpanTreeTemplate::Instance() {
    static SpanTreeTemplate instance;
    return instance;
}

void SpanTreeTemplate::InitDefaults() {
    RegisterService("proxy", "proxy_e2e", {
        {"feature_rpc", "feature", "feature_total"},
        {"recall_rpc", "recall", "recall_total"},
        {"precalc_rpc", "precalc", "precalc_total"},
        {"rank_rpc", "rank_master", "rank_master_total"},
        {"recall_precalc_parallel_wait"},
    });

    RegisterService("feature", "feature_total", {
        {"generate_user_logs"},
        {"*", "", "", true},
    });

    RegisterService("recall", "recall_total", {
        {"vllm_rpc"},
        {"recall_to_vllm_brpc"},
        {"vllm_parse"},
        {"kv_init"},
        {"kvcache_exist"},
        {"kvcache_get"},
        {"*", "", "", true},
    });

    RegisterService("precalc", "precalc_total", {
        {"kv_init"},
        {"kv_create"},
        {"kv_memcpy"},
        {"kv_set"},
        {"kv_write_total"},
        {"generate_result"},
        {"*", "", "", true},
    });

    RegisterService("rank_master", "rank_master_total", {
        {"kv_get"},
        {"sub_worker_rpc", "rank_sub", "rank_sub_total"},
        {"*", "", "", true},
    });

    RegisterService("rank_sub", "rank_sub_total", {
        {"score_skus"},
        {"*", "", "", true},
    });
}

void SpanTreeTemplate::RegisterService(
    const std::string& service,
    const std::string& root_stage,
    const std::vector<Edge>& edges) {
    services_[service] = {root_stage, edges};
}

std::string SpanTreeTemplate::ParentStage(
    const std::string& service, const std::string& stage) const {
    auto it = services_.find(service);
    if (it == services_.end()) return "";

    // If it's the root stage itself, no parent in this service
    if (stage == it->second.root_stage) return "";

    for (auto& e : it->second.edges) {
        if (e.stage == stage) return it->second.root_stage;
        if (e.wildcard) return it->second.root_stage;  // wildcard catches unknown
    }
    // Fallback: attach to root
    return it->second.root_stage;
}

std::pair<std::string, std::string> SpanTreeTemplate::CrossService(
    const std::string& service, const std::string& stage) const {
    auto it = services_.find(service);
    if (it == services_.end()) return {"", ""};

    for (auto& e : it->second.edges) {
        if (e.stage == stage && !e.cross_service.empty()) {
            return {e.cross_service, e.cross_root};
        }
    }
    return {"", ""};
}

std::string SpanTreeTemplate::RootStage(const std::string& service) const {
    auto it = services_.find(service);
    if (it == services_.end()) return "";
    return it->second.root_stage;
}

// ---- Tree Building ----

static SpanTreeNode* FindOrCreateNode(
    SpanTreeNode* root,
    const std::string& service,
    const std::string& stage,
    const common::perf::Span* span) {

    // Breadth-first search for existing node with same service+stage
    std::vector<SpanTreeNode*> queue;
    queue.push_back(root);
    while (!queue.empty()) {
        auto* n = queue.front();
        queue.erase(queue.begin());
        if (!n->has_span && n->service == service && n->stage == stage) {
            // Found the virtual parent, attach span data
            n->span = *span;
            n->has_span = true;
            return n;
        }
        for (auto& c : n->children) {
            queue.push_back(&c);
        }
    }

    // Not found — create new leaf
    SpanTreeNode leaf;
    leaf.service = service;
    leaf.stage = stage;
    leaf.span = span ? *span : common::perf::Span{};
    leaf.has_span = span != nullptr;

    root->children.push_back(std::move(leaf));
    return &root->children.back();
}

SpanTreeNode BuildSpanTree(const std::vector<common::perf::Span>& spans) {
    SpanTreeNode root;
    root.service = "";
    root.stage = "trace";

    auto& tmpl = SpanTreeTemplate::Instance();

    // Group spans by service
    std::map<std::string, std::vector<common::perf::Span>> by_service;
    for (auto& s : spans) {
        by_service[std::string(s.service)].push_back(s);
    }

    // First pass: create service roots and direct children per service
    std::map<std::string, SpanTreeNode*> service_roots;
    for (auto& [svc, svc_spans] : by_service) {
        std::string root_stage = tmpl.RootStage(svc);
        if (root_stage.empty()) root_stage = svc + "_total";

        // Find or create the service root
        common::perf::Span root_span;
        bool has_root = false;
        for (auto& s : svc_spans) {
            if (std::string(s.stage) == root_stage) {
                root_span = s;
                has_root = true;
                break;
            }
        }

        auto* svc_root = FindOrCreateNode(&root, svc, root_stage,
                                           has_root ? &root_span : nullptr);
        service_roots[svc] = svc_root;

        // Attach each span to its parent within the service
        for (auto& s : svc_spans) {
            if (std::string(s.stage) == root_stage) continue;
            std::string parent = tmpl.ParentStage(svc, std::string(s.stage));
            if (parent.empty()) parent = root_stage;

            SpanTreeNode* parent_node = svc_root;
            // Walk the children to find the parent node
            std::vector<SpanTreeNode*> q;
            q.push_back(svc_root);
            while (!q.empty()) {
                auto* n = q.front();
                q.erase(q.begin());
                if (n->stage == parent) {
                    parent_node = n;
                    break;
                }
                for (auto& c : n->children) q.push_back(&c);
            }

            SpanTreeNode leaf;
            leaf.service = svc;
            leaf.stage = s.stage;
            leaf.span = s;
            leaf.has_span = true;
            parent_node->children.push_back(std::move(leaf));
        }
    }

    // Second pass: resolve cross-service edges
    for (auto& [svc, svc_root] : service_roots) {
        // Check each child for cross-service links
        for (auto& child : svc_root->children) {
            auto [cross_svc, cross_root_stage] =
                tmpl.CrossService(svc, child.stage);
            if (!cross_svc.empty() && service_roots.count(cross_svc)) {
                // Move the cross-service subtree as children of this node
                auto* cross_root = service_roots[cross_svc];
                // Find children of cross_root and move them under child
                child.children = std::move(cross_root->children);
            }
        }
    }

    // Remove empty placeholder children
    root.children.erase(
        std::remove_if(root.children.begin(), root.children.end(),
                       [](auto& c) { return !c.has_span && c.children.empty(); }),
        root.children.end());

    return root;
}

} // namespace perf
