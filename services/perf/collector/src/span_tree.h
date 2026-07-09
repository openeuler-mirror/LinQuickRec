#ifndef PERF_COLLECTOR_SPAN_TREE_H
#define PERF_COLLECTOR_SPAN_TREE_H

#include "common/perf_registry.h"

#include <map>
#include <string>
#include <vector>

namespace perf {

struct SpanTreeNode {
    std::string service;
    std::string stage;
    common::perf::Span span;        // empty if this is a virtual parent
    bool has_span = false;          // true if span data was filled
    std::vector<SpanTreeNode> children;
};

// Returns a template tree. Edges marked as "*" match any unknown stage
// under the current service's root span.
// cross_service: {"service": "other", "root": "other_root"} means
// spans from "other" service are attached as children of this node.
SpanTreeNode BuildSpanTree(
    const std::vector<common::perf::Span>& spans);

// Build the default hierarchy template.
// Unknown stages are automatically attached to their service's root
// via the "*" wildcard entry.
class SpanTreeTemplate {
public:
    struct Edge {
        std::string stage;
        std::string cross_service;  // non-empty if this edge points to another service
        std::string cross_root;      // root stage in the other service
        bool wildcard = false;       // "*" matches any unknown stage
    };

    static SpanTreeTemplate& Instance();

    // Register a service's root stage and its sub-stages
    void RegisterService(const std::string& service,
                         const std::string& root_stage,
                         const std::vector<Edge>& edges);

    // Find the parent for a given (service, stage). Returns empty string if
    // the stage is the root for its service.
    std::string ParentStage(const std::string& service,
                            const std::string& stage) const;

    // Get the cross-service reference for a (service, stage) if any
    std::pair<std::string, std::string> CrossService(
        const std::string& service, const std::string& stage) const;

    // Get the root stage for a service
    std::string RootStage(const std::string& service) const;

private:
    SpanTreeTemplate();
    void InitDefaults();

    struct ServiceNode {
        std::string root_stage;
        std::vector<Edge> edges;
    };
    std::map<std::string, ServiceNode> services_;
};

} // namespace perf

#endif // PERF_COLLECTOR_SPAN_TREE_H
