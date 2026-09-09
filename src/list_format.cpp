#include "teez/cli/list_format.hpp"

#include <map>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

namespace teez::cli {

namespace {

struct TreeNode {
    std::map<std::string, TreeNode> children;
};

void split_path_parts(const std::string& id, std::vector<std::string>& parts) {
    parts.clear();
    if (id.find("::") != std::string::npos) {
        std::size_t start = 0;
        while (start <= id.size()) {
            const auto separator = id.find("::", start);
            parts.push_back(id.substr(start, separator == std::string::npos ? std::string::npos
                                                                            : separator - start));
            if (separator == std::string::npos) {
                break;
            }
            start = separator + 2;
        }
        return;
    }

    if (id.find('/') != std::string::npos) {
        std::size_t start = 0;
        while (start <= id.size()) {
            const auto separator = id.find('/', start);
            parts.push_back(id.substr(start, separator == std::string::npos ? std::string::npos
                                                                            : separator - start));
            if (separator == std::string::npos) {
                break;
            }
            start = separator + 1;
        }
        return;
    }

    parts.push_back(id);
}

void insert_path(TreeNode& root, const std::string& id) {
    std::vector<std::string> parts;
    split_path_parts(id, parts);

    TreeNode* node = &root;
    for (const auto& part : parts) {
        node = &node->children[part];
    }
}

void write_tree_node(const TreeNode& node, const std::string& prefix, std::ostream& out) {
    auto it = node.children.begin();
    const auto end = node.children.end();

    while (it != end) {
        const bool is_last = std::next(it) == end;
        out << prefix << (is_last ? "└── " : "├── ") << it->first << '\n';

        if (!it->second.children.empty()) {
            write_tree_node(it->second, prefix + (is_last ? "    " : "│   "), out);
        }

        ++it;
    }
}

} // namespace

void write_test_list(const std::vector<std::string>& tests, const ListOutputOptions& options,
                     std::ostream& out) {
    if (options.json) {
        for (const auto& id : tests) {
            out << nlohmann::json{{"event", "test"}, {"id", id}}.dump() << '\n';
        }
        return;
    }

    if (options.tree) {
        TreeNode root;
        for (const auto& id : tests) {
            insert_path(root, id);
        }
        write_tree_node(root, "", out);
        return;
    }

    for (const auto& id : tests) {
        out << id << '\n';
    }
}

} // namespace teez::cli
