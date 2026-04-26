// StoreKit 2 IAP for iOS. Compiled only on Apple platforms; non-Apple builds
// pull in the stub-only fallback below.

#include "engine/platform/ios_storekit.h"
#include "engine/core/logger.h"

#if defined(__APPLE__)
#  include <TargetConditionals.h>
#endif

namespace fate {

const std::vector<std::string>& IosStoreKit::opalsProductIds() {
    // Match App Store Connect IAP catalog. These IDs are placeholders pending
    // catalog setup. The server-side `opals_shop` table maps amount → opals.
    static const std::vector<std::string> ids = {
        "fatemmo.opals.tier1_500",     // 500 opals — $4.99
        "fatemmo.opals.tier2_1200",    // 1,200 opals — $9.99
        "fatemmo.opals.tier3_3000",    // 3,000 opals — $19.99
        "fatemmo.opals.tier4_6500",    // 6,500 opals — $39.99
        "fatemmo.opals.tier5_15000",   // 15,000 opals — $79.99
    };
    return ids;
}

#if defined(__APPLE__) && TARGET_OS_IOS

// Real implementations live in ios_storekit_impl.mm (Objective-C++ + StoreKit
// imports). Keeping this .mm minimal so the stub pattern stays clear; the
// next session can flesh out the StoreKit 2 calls without touching this
// dispatch layer.

void IosStoreKit::fetchProducts(const std::vector<std::string>& productIds,
                                 std::function<void(std::vector<StoreKitProduct>)> onComplete) {
    // TODO: implement via Product.products(for:) (StoreKit 2). Requires Swift
    // bridging or @available(iOS 15.0+) Objective-C++ syntax.
    LOG_INFO("StoreKit", "fetchProducts() not yet implemented; returning empty");
    if (onComplete) onComplete({});
    (void)productIds;
}

void IosStoreKit::purchase(const std::string& productId,
                            std::function<void(StoreKitPurchaseResult)> onComplete) {
    LOG_INFO("StoreKit", "purchase(%s) not yet implemented", productId.c_str());
    StoreKitPurchaseResult r;
    r.success = false;
    r.productId = productId;
    r.errorMessage = "StoreKit 2 integration pending";
    if (onComplete) onComplete(std::move(r));
}

void IosStoreKit::restorePurchases(std::function<void(std::vector<StoreKitPurchaseResult>)> onComplete) {
    LOG_INFO("StoreKit", "restorePurchases() not yet implemented");
    if (onComplete) onComplete({});
}

#else // non-iOS: stub-out for desktop/Android builds.

void IosStoreKit::fetchProducts(const std::vector<std::string>&,
                                 std::function<void(std::vector<StoreKitProduct>)> onComplete) {
    if (onComplete) onComplete({});
}
void IosStoreKit::purchase(const std::string& productId,
                            std::function<void(StoreKitPurchaseResult)> onComplete) {
    StoreKitPurchaseResult r;
    r.success = false;
    r.productId = productId;
    r.errorMessage = "IAP only available on iOS";
    if (onComplete) onComplete(std::move(r));
}
void IosStoreKit::restorePurchases(std::function<void(std::vector<StoreKitPurchaseResult>)> onComplete) {
    if (onComplete) onComplete({});
}

#endif

} // namespace fate
