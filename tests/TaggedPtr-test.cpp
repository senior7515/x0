#include <gtest/gtest.h>
#include <x0/TaggedPtr.h>

using x0::TaggedPtr;

class TaggedPtrTest : public ::testing::Test
{
public:
	void SetUp();
	void TearDown();

	void Base();
};

void TaggedPtrTest::SetUp()
{
}

void TaggedPtrTest::TearDown()
{
	TaggedPtr<std::string> tp;

	EXPECT_EQ(nullptr, tp.get());
	EXPECT_EQ(nullptr, tp.ptr());
	EXPECT_EQ(0, tp.tag());
}

TEST_F(TaggedPtrTest, GetAndSet)
{
	std::string p1("p1"), p2("p2");
	TaggedPtr<std::string> tp(&p1, 42);

	EXPECT_EQ(42, tp.tag());
	EXPECT_EQ(&p1, tp.ptr());
	EXPECT_EQ(&p1, tp.get());

	tp.set(&p2, 13);

	EXPECT_EQ(13, tp.tag());
	EXPECT_EQ(&p2, tp.ptr());
	EXPECT_EQ(&p2, tp.get());
}

TEST_F(TaggedPtrTest, ToBool)
{
	std::string p("fnord");
	TaggedPtr<std::string> tp1(nullptr, 42);
	TaggedPtr<std::string> tp2(&p, 42);

	EXPECT_FALSE(tp1);
	EXPECT_TRUE(tp2);
}

TEST_F(TaggedPtrTest, Not)
{
	std::string p("fnord");
	TaggedPtr<std::string> tp1(nullptr, 42);
	TaggedPtr<std::string> tp2(&p, 42);

	EXPECT_TRUE(!tp1);
	EXPECT_FALSE(!tp2);
}

TEST_F(TaggedPtrTest, Equal)
{
	std::string fnord("fnord");
	TaggedPtr<std::string> tp1(&fnord, 42);
	TaggedPtr<std::string> tp2(&fnord, 42);

	EXPECT_EQ(tp1, tp2);
}

TEST_F(TaggedPtrTest, UnEqual)
{
	std::string p("fnord");
	std::string u("fnord");
	TaggedPtr<std::string> tp1(&p, 42);
	TaggedPtr<std::string> tp2(&p, 43);
	TaggedPtr<std::string> tp3(&u, 42);

	EXPECT_NE(tp1, tp2);
	EXPECT_NE(tp1, tp3);
}
