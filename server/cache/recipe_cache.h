#pragma once
#include <string>
#include <vector>
#include <unordered_map>

namespace pqxx { class connection; }

namespace fate {

struct RecipeIngredient {
    std::string itemId;
    int quantity = 1;
};

struct CachedRecipe {
    std::string recipeId;
    std::string recipeName;
    int bookTier = 0;        // 0=Novice, 1=Book I, 2=Book II, 3=Book III
    std::string resultItemId;
    int resultQuantity = 1;
    int levelReq = 1;
    std::string classReq;    // empty = any class
    int goldCost = 0;
    std::vector<RecipeIngredient> ingredients;
};

class RecipeCache {
public:
    bool loadFromDatabase(pqxx::connection& conn);

    void addRecipe(const CachedRecipe& recipe) {
        recipes_[recipe.recipeId] = recipe;
    }

    const CachedRecipe* getRecipe(const std::string& recipeId) const {
        auto it = recipes_.find(recipeId);
        return it != recipes_.end() ? &it->second : nullptr;
    }

    std::vector<const CachedRecipe*> getRecipesForTier(int tier) const {
        std::vector<const CachedRecipe*> result;
        for (const auto& [id, recipe] : recipes_) {
            if (recipe.bookTier == tier) result.push_back(&recipe);
        }
        return result;
    }

    std::vector<const CachedRecipe*> getAllRecipes() const {
        std::vector<const CachedRecipe*> result;
        for (const auto& [id, recipe] : recipes_) {
            result.push_back(&recipe);
        }
        return result;
    }

    size_t size() const { return recipes_.size(); }

    void clear() { recipes_.clear(); }

private:
    std::unordered_map<std::string, CachedRecipe> recipes_;
};

} // namespace fate
