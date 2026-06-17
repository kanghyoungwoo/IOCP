#include <gtest/gtest.h>

#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>

#include "../UserManager.h"
#include "../ErrorCode.h"

class UserManagerTest : public ::testing::Test
{
protected:
    UserManager mgr;

    static constexpr UINT32 MAX_USERS = 10;

    void SetUp() override
    {
        mgr.Init(MAX_USERS);
    }
};

// ==================== Init ====================

TEST_F(UserManagerTest, InitSetsMaxUserCount)
{
    EXPECT_EQ(mgr.GetMaxUserCnt(), (INT32)MAX_USERS);
}

TEST_F(UserManagerTest, InitCurrentUserCountIsZero)
{
    EXPECT_EQ(mgr.GetCurrentUserCnt(), 0);
}

TEST_F(UserManagerTest, InitUserPoolAccessible)
{
    for (UINT32 i = 0; i < MAX_USERS; ++i)
    {
        SCOPED_TRACE(i);
        User* user = mgr.GetUserByConnIdx(i);
        ASSERT_NE(user, nullptr);
        EXPECT_EQ(user->GetNetConnIndex(), (INT32)i);
    }
}

// ==================== AddUser ====================

TEST_F(UserManagerTest, AddUserSucceeds)
{
    char id[] = "player1";
    ERROR_CODE result = mgr.Adduser(id, 0);
    EXPECT_EQ(result, ERROR_CODE::NONE);
}

TEST_F(UserManagerTest, AddUserSetsLoginState)
{
    char id[] = "player1";
    mgr.Adduser(id, 0);
    EXPECT_EQ(mgr.GetUserByConnIdx(0)->GetUserID(), "player1");
}

TEST_F(UserManagerTest, AddDuplicateUserIDFails)
{
    char id[] = "player1";
    mgr.Adduser(id, 0);

    char id2[] = "player1";
    ERROR_CODE result = mgr.Adduser(id2, 1);
    EXPECT_EQ(result, ERROR_CODE::LOGIN_USER_ALREADY);
}

TEST_F(UserManagerTest, DifferentUsersCanBeAdded)
{
    char id0[] = "player0";
    char id1[] = "player1";
    EXPECT_EQ(mgr.Adduser(id0, 0), ERROR_CODE::NONE);
    EXPECT_EQ(mgr.Adduser(id1, 1), ERROR_CODE::NONE);
}

// ==================== DeleteUserInfo ====================

TEST_F(UserManagerTest, DeleteUserInfoRemovesFromDictionary)
{
    char id[] = "player1";
    mgr.Adduser(id, 0);

    User* user = mgr.GetUserByConnIdx(0);
    mgr.DeleteUserInfo(user);

    // 삭제 후 동일 ID로 재등록 가능해야 함
    char id2[] = "player1";
    EXPECT_EQ(mgr.Adduser(id2, 0), ERROR_CODE::NONE);
}

TEST_F(UserManagerTest, DeleteUserInfoClearsUserState)
{
    char id[] = "player1";
    mgr.Adduser(id, 0);

    User* user = mgr.GetUserByConnIdx(0);
    mgr.DeleteUserInfo(user);

    EXPECT_EQ(user->GetUserID(), "");
}

TEST_F(UserManagerTest, DeleteUserInfoDecreasesCount)
{
    char id[] = "player1";
    mgr.Adduser(id, 0);
    mgr.IncreaseUserCnt();

    mgr.DeleteUserInfo(mgr.GetUserByConnIdx(0));
    EXPECT_EQ(mgr.GetCurrentUserCnt(), 0);
}

// ==================== FindUserIndexByID ====================

TEST_F(UserManagerTest, FindExistingUserReturnsIndex)
{
    char id[] = "player1";
    mgr.Adduser(id, 3);

    EXPECT_EQ(mgr.FindUserIndexByID(id), 3);
}

TEST_F(UserManagerTest, FindNonExistingUserReturnsMinusOne)
{
    char id[] = "ghost";
    EXPECT_EQ(mgr.FindUserIndexByID(id), -1);
}

TEST_F(UserManagerTest, FindAfterDeleteReturnsMinusOne)
{
    char id[] = "player1";
    mgr.Adduser(id, 0);
    mgr.DeleteUserInfo(mgr.GetUserByConnIdx(0));

    EXPECT_EQ(mgr.FindUserIndexByID(id), -1);
}

// ==================== IncreaseUserCnt / DecreaseUserCnt ====================

TEST_F(UserManagerTest, IncreaseUserCntIncrements)
{
    mgr.IncreaseUserCnt();
    mgr.IncreaseUserCnt();
    EXPECT_EQ(mgr.GetCurrentUserCnt(), 2);
}

TEST_F(UserManagerTest, DecreaseUserCntDecrements)
{
    mgr.IncreaseUserCnt();
    mgr.IncreaseUserCnt();
    mgr.DecreaseUserCnt();
    EXPECT_EQ(mgr.GetCurrentUserCnt(), 1);
}

TEST_F(UserManagerTest, DecreaseUserCntDoesNotGoBelowZero)
{
    EXPECT_EQ(mgr.GetCurrentUserCnt(), 0);
    mgr.DecreaseUserCnt();
    EXPECT_EQ(mgr.GetCurrentUserCnt(), 0);
}
