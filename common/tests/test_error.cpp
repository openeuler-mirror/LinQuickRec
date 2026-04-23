#include "common/error.h"
#include <iostream>
#include <cassert>

int main() {
    std::cout << "=== Test Error Code System ===" << std::endl;

    // 1. OK status
    {
        common::error::Status ok = common::error::Status::OK();
        assert(ok.IsOk());
        assert(!ok.IsError());
        assert(ok.Code() == common::error::common_errors::SUCCESS);
        assert(ok.ToString() == "OK");
        assert(ok.GetModule() == common::error::ModuleCode::COMMON);
        assert(ok.GetType() == common::error::ErrorType::SUCCESS);
        std::cout << "[PASS] OK status" << std::endl;
    }

    // 2. Error status
    {
        auto err = common::error::Status::Error(
            common::error::common_errors::INVALID_ARGUMENT,
            "Invalid parameter 'user_id'");
        assert(err.IsError());
        assert(!err.IsOk());
        assert(err.Code() == common::error::common_errors::INVALID_ARGUMENT);
        assert(err.Message() == "Invalid parameter 'user_id'");
        std::cout << "[PASS] Error status: " << err.ToString() << std::endl;
    }

    // 3. Convenience error functions
    {
        auto e1 = common::error::InvalidArgumentError("bad arg");
        assert(e1.GetType() == common::error::ErrorType::INVALID_INPUT);

        auto e2 = common::error::ResourceError("out of memory");
        assert(e2.GetType() == common::error::ErrorType::RESOURCE_ERROR);

        auto e3 = common::error::NotFoundError("file missing");
        assert(e3.GetType() == common::error::ErrorType::NOT_FOUND);

        auto e4 = common::error::TimeoutError("request timeout");
        assert(e4.GetType() == common::error::ErrorType::TIMEOUT);

        auto e5 = common::error::NetworkError("connection reset");
        assert(e5.GetType() == common::error::ErrorType::NETWORK_ERROR);

        auto e6 = common::error::InternalError("unexpected");
        assert(e6.GetType() == common::error::ErrorType::INTERNAL);

        std::cout << "[PASS] All convenience error functions" << std::endl;
    }

    // 4. Module and type extraction
    {
        uint32_t code = common::error::MakeErrorCode(
            common::error::ModuleCode::RECALL,
            common::error::ErrorType::SERVICE_ERROR,
            0x0001);
        assert(common::error::GetModuleFromCode(code) == common::error::ModuleCode::RECALL);
        assert(common::error::GetTypeFromCode(code) == common::error::ErrorType::SERVICE_ERROR);
        assert(common::error::GetSpecificCodeFromCode(code) == 0x0001);
        std::cout << "[PASS] Module/Type/SpecificCode extraction" << std::endl;
    }

    // 5. All module codes
    {
        auto status = common::error::Status::Error(
            common::error::ModuleCode::PRECALC,
            common::error::ErrorType::TIMEOUT,
            0x0002, "precalc timeout");
        assert(status.GetModule() == common::error::ModuleCode::PRECALC);
        assert(common::error::ModuleToString(status.GetModule()) == "PRECALC");
        std::cout << "[PASS] ModuleCode: " << common::error::ModuleToString(status.GetModule()) << std::endl;
    }

    // 6. Error type to string
    {
        assert(std::string(common::error::ErrorTypeToString(common::error::ErrorType::CONFIG_ERROR)) == "CONFIG_ERROR");
        assert(std::string(common::error::ErrorTypeToString(common::error::ErrorType::UNAUTHORIZED)) == "UNAUTHORIZED");
        std::cout << "[PASS] ErrorTypeToString" << std::endl;
    }

    // 7. Comparison operators
    {
        common::error::Status a = common::error::Status::OK();
        common::error::Status b = common::error::Status::OK();
        common::error::Status c = common::error::InvalidArgumentError("x");
        assert(a == b);
        assert(a != c);
        assert(a == common::error::common_errors::SUCCESS);
        assert(c != common::error::common_errors::SUCCESS);
        std::cout << "[PASS] Comparison operators" << std::endl;
    }

    // 8. Bool conversion
    {
        common::error::Status ok;
        common::error::Status err = common::error::InternalError("fail");
        assert(ok);
        assert(!err);
        std::cout << "[PASS] Bool conversion" << std::endl;
    }

    // 9. ErrorCodeToString
    {
        std::string str = common::error::ErrorCodeToString(
            common::error::common_errors::PERMISSION_DENIED);
        assert(!str.empty());
        std::cout << "[PASS] ErrorCodeToString: " << str << std::endl;
    }

    // 10. Status copy and assign
    {
        common::error::Status original = common::error::NotFoundError("original msg");
        common::error::Status copied(original);
        assert(copied == original);
        common::error::Status assigned;
        assigned = original;
        assert(assigned == original);
        std::cout << "[PASS] Copy and assignment" << std::endl;
    }

    std::cout << "\n=== All Error Code Tests Passed ===" << std::endl;
    return 0;
}
