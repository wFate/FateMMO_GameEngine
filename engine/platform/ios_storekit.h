#pragma once
#include <string>
#include <vector>
#include <functional>
#include <cstdint>

// =============================================================================
// StoreKit 2 — Opals IAP integration (iOS only).
// =============================================================================
// Compiles to no-op stubs on non-iOS platforms. Implementation in
// engine/platform/ios_storekit.mm uses async StoreKit 2 APIs (Swift-bridged
// where needed). The product IDs MUST match the App Store Connect IAP catalog
// or fetchProducts will return empty.
//
// Server-side validation: every successful purchase forwards its
// transactionId + productId to the FateMMO auth server (separate packet),
// which queries Apple's `/verifyReceipt` (or StoreKit 2 server API) before
// crediting opals. Never trust the client's purchase claim alone.
// =============================================================================

namespace fate {

struct StoreKitProduct {
    std::string productId;       // matches App Store Connect Catalog (e.g., "opals_500")
    std::string displayName;     // localized title
    std::string formattedPrice;  // localized "$4.99" / "€4,99"
    int64_t opalsAmount = 0;     // server-derived; 0 if metadata missing
};

struct StoreKitPurchaseResult {
    bool success = false;
    std::string productId;
    std::string transactionId;     // for server validation
    std::string errorMessage;
};

class IosStoreKit {
public:
    // List of opals tiers. Add IDs here as they're created in App Store Connect.
    static const std::vector<std::string>& opalsProductIds();

    // Async fetch product metadata. Callback fires on the SDL/main thread once
    // the StoreKit query returns. Empty vector = network error or unknown IDs.
    // No-op (immediate empty callback) on non-iOS platforms.
    static void fetchProducts(const std::vector<std::string>& productIds,
                              std::function<void(std::vector<StoreKitProduct>)> onComplete);

    // Initiate a purchase. Callback fires on success/failure/cancel.
    // Caller (game UI) owns the "spinner" UX during the round-trip.
    // No-op (immediate failure callback) on non-iOS platforms.
    static void purchase(const std::string& productId,
                         std::function<void(StoreKitPurchaseResult)> onComplete);

    // Restore previous purchases (consumable opals don't restore — this is for
    // future non-consumables like cosmetic packs). Calls back per restored item.
    static void restorePurchases(std::function<void(std::vector<StoreKitPurchaseResult>)> onComplete);
};

} // namespace fate
