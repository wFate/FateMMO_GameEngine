#include "server/cache/recipe_cache.h"
#include "engine/core/logger.h"
#include <pqxx/pqxx>

namespace fate {

bool RecipeCache::loadFromDatabase(pqxx::connection& conn) {
    try {
        pqxx::work txn(conn);

        // Load recipes — book_tier may not exist yet; use a fallback query if it fails
        pqxx::result recipes;
        try {
            recipes = txn.exec(
                "SELECT recipe_id, recipe_name, "
                "COALESCE(book_tier, 0) AS book_tier, "
                "result_item_id, result_quantity, level_req, class_req, gold_cost "
                "FROM crafting_recipes"
            );
        } catch (const std::exception&) {
            // book_tier column doesn't exist yet — retry without it
            recipes = txn.exec(
                "SELECT recipe_id, recipe_name, "
                "0 AS book_tier, "
                "result_item_id, result_quantity, level_req, class_req, gold_cost "
                "FROM crafting_recipes"
            );
        }

        for (const auto& row : recipes) {
            CachedRecipe recipe;
            recipe.recipeId      = row["recipe_id"].as<std::string>();
            recipe.recipeName    = row["recipe_name"].as<std::string>();
            recipe.bookTier      = row["book_tier"].as<int>(0);
            recipe.resultItemId  = row["result_item_id"].as<std::string>();
            recipe.resultQuantity = row["result_quantity"].as<int>(1);
            recipe.levelReq      = row["level_req"].as<int>(1);
            recipe.classReq      = row["class_req"].is_null() ? "" : row["class_req"].as<std::string>();
            recipe.goldCost      = row["gold_cost"].as<int>(0);
            addRecipe(recipe);
        }

        // Load ingredients and attach to their recipes
        auto ingredients = txn.exec(
            "SELECT recipe_id, item_id, quantity FROM crafting_ingredients"
        );

        for (const auto& row : ingredients) {
            std::string recipeId = row["recipe_id"].as<std::string>();
            auto it = recipes_.find(recipeId);
            if (it != recipes_.end()) {
                RecipeIngredient ing;
                ing.itemId   = row["item_id"].as<std::string>();
                ing.quantity = row["quantity"].as<int>(1);
                it->second.ingredients.push_back(ing);
            }
        }

        txn.commit();
        LOG_INFO("RecipeCache", "Loaded %zu crafting recipes", recipes_.size());
        return true;
    } catch (const std::exception& e) {
        LOG_WARN("RecipeCache", "Failed to load crafting recipes: %s", e.what());
        return false;
    }
}

} // namespace fate
