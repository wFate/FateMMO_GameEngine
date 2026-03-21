#include <doctest/doctest.h>
#include "server/cache/recipe_cache.h"

using namespace fate;

TEST_SUITE("RecipeCache") {

TEST_CASE("Empty cache returns nullptr for unknown recipe") {
    RecipeCache cache;
    CHECK(cache.getRecipe("nonexistent") == nullptr);
}

TEST_CASE("Can add and retrieve a recipe") {
    RecipeCache cache;
    CachedRecipe recipe;
    recipe.recipeId = "test_recipe";
    recipe.recipeName = "Test Sword";
    recipe.bookTier = 1;
    recipe.resultItemId = "item_test_sword";
    recipe.resultQuantity = 1;
    recipe.levelReq = 5;
    recipe.goldCost = 1000;
    recipe.ingredients.push_back({"mat_core_2nd", 3});

    cache.addRecipe(recipe);

    auto* found = cache.getRecipe("test_recipe");
    REQUIRE(found != nullptr);
    CHECK(found->recipeName == "Test Sword");
    CHECK(found->bookTier == 1);
    CHECK(found->ingredients.size() == 1);
    CHECK(found->ingredients[0].itemId == "mat_core_2nd");
    CHECK(found->ingredients[0].quantity == 3);
}

TEST_CASE("getRecipesForTier returns correct recipes") {
    RecipeCache cache;

    CachedRecipe r1;
    r1.recipeId = "r1";
    r1.recipeName = "Recipe 1";
    r1.bookTier = 0;
    r1.resultItemId = "item_1";

    CachedRecipe r2;
    r2.recipeId = "r2";
    r2.recipeName = "Recipe 2";
    r2.bookTier = 1;
    r2.resultItemId = "item_2";

    CachedRecipe r3;
    r3.recipeId = "r3";
    r3.recipeName = "Recipe 3";
    r3.bookTier = 1;
    r3.resultItemId = "item_3";

    cache.addRecipe(r1);
    cache.addRecipe(r2);
    cache.addRecipe(r3);

    auto tier0 = cache.getRecipesForTier(0);
    CHECK(tier0.size() == 1);

    auto tier1 = cache.getRecipesForTier(1);
    CHECK(tier1.size() == 2);

    auto tier2 = cache.getRecipesForTier(2);
    CHECK(tier2.size() == 0);
}

TEST_CASE("size() returns correct count") {
    RecipeCache cache;
    CHECK(cache.size() == 0);

    CachedRecipe r;
    r.recipeId = "r1";
    r.resultItemId = "item_1";
    cache.addRecipe(r);
    CHECK(cache.size() == 1);
}

TEST_CASE("Recipe with class requirement") {
    RecipeCache cache;
    CachedRecipe r;
    r.recipeId = "warrior_only";
    r.recipeName = "Warrior Sword";
    r.classReq = "Warrior";
    r.resultItemId = "item_warrior_sword";
    cache.addRecipe(r);

    auto* found = cache.getRecipe("warrior_only");
    REQUIRE(found != nullptr);
    CHECK(found->classReq == "Warrior");
}

TEST_CASE("Recipe with empty class req means any class") {
    RecipeCache cache;
    CachedRecipe r;
    r.recipeId = "universal";
    r.recipeName = "Universal Potion";
    r.classReq = "";
    r.resultItemId = "item_potion";
    cache.addRecipe(r);

    auto* found = cache.getRecipe("universal");
    REQUIRE(found != nullptr);
    CHECK(found->classReq.empty());
}

} // TEST_SUITE
