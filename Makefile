# ---- Library and Project names -----------------------------------------------
NAME			= ircserv
DEBUG_NAME		= ircserv_debug
# BONUS			= ircserv_bonus
# DEBUG_BONUS	= ircserv_bonus_debug

# ---- Sources & Objects -------------------------------------------------------
SRC_DIR			= srcs
INC_DIR			= includes
BUILD_DIR		= build
DEBUG_BUILD_DIR	= build_debug

SRCS_ROOT		= main

SRCS_SERVER 	= Server \
				  Socket \
				  Client \
				  ClientManager \
				  Channel \
				  ChannelManager \
				  Logger \
				  SignalHandler

SRCS_COMMANDS	= CommandProcessor \
				  Handlers

SRCS_UTILS		= MemberSet \
				  ModeSet
# 				  Parser \
# 				  Reply

# SRCS_BONUS		= DCC \
# 				  Bot

SRCS			= $(SRCS_ROOT) \
				  $(addprefix server/, $(SRCS_SERVER)) \
				  $(addprefix commands/, $(SRCS_COMMANDS)) \
				  $(addprefix utils/, $(SRCS_UTILS))

OBJS			= $(addprefix $(BUILD_DIR)/$(SRC_DIR)/, $(addsuffix .o, $(SRCS)))
DEBUG_OBJS		= $(addprefix $(DEBUG_BUILD_DIR)/$(SRC_DIR)/, $(addsuffix .o, $(SRCS)))
# BONUS_OBJS		= $(OBJS:.o=.bonus.o)
# DEBUG_BONUS_OBJS	= $(DEBUG_OBJS:.o=.bonus.o)

# ---- Auto-generated header deps ----------------------------------------------

DEPS			= $(OBJS:.o=.d) $(DEBUG_OBJS:.o=.d)
# BONUS_DEPS	= $(BONUS_OBJS:.o=.d) $(DEBUG_BONUS_OBJS:.o=.d)

# ---- Compiler and Flags ------------------------------------------------------

CXX				= c++
CXXFLAGS		= -Wall -Wextra -Werror -pedantic -std=c++98
IFLAGS			= -I$(INC_DIR)
DFLAGS			= -DDEBUG -g3 -O0 -fsanitize=address
DEBUG_CXXFLAGS	= $(CXXFLAGS) $(DFLAGS)

# ---- Rules -------------------------------------------------------------------

all: $(NAME)

# bonus: $(BONUS)

debug: $(DEBUG_NAME)

# debug_bonus: $(DEBUG_BONUS)

# clang-format
format:
	@find srcs includes -type f \( -name "*.cpp" -o -name "*.hpp" \) -print0 \
	| xargs -0 clang-format -i

# Usage: make tidy FILE=srcs/main.cpp or make tidy FILE="srcs/a.cpp srcs/b.cpp"
tidy:
	@if [ -z "$(FILE)" ]; then echo "Usage: make tidy FILE=srcs/main.cpp"; exit 1; fi
	@clang-tidy $(FILE) -- $(CXXFLAGS) $(IFLAGS)

tidy-all:
	@find srcs includes -type f \( -name "*.cpp" -o -name "*.hpp" \) -print0 \
	  | xargs -0 -I {} clang-tidy {} -- $(CXXFLAGS) $(IFLAGS)

iwyu: fclean
	make -k CXX=include-what-you-use CXXFLAGS="-Xiwyu --error_always"

$(NAME): $(OBJS)
	$(CXX) $(CXXFLAGS) $(OBJS) -o $@

$(DEBUG_NAME): $(DEBUG_OBJS)
	$(CXX) $(DEBUG_CXXFLAGS) $(DEBUG_OBJS) -o $@

# $(BONUS): $(BONUS_OBJS)
# 	$(CXX) $(CXXFLAGS) $(BONUS_OBJS) -o $@

# $(DEBUG_BONUS): $(DEBUG_BONUS_OBJS)
# 	$(CXX) $(DEBUG_CXXFLAGS) $(DEBUG_BONUS_OBJS) -o $@

$(BUILD_DIR)/$(SRC_DIR)/%.o: $(SRC_DIR)/%.cpp
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) $(IFLAGS) -MMD -MP -c $< -o $@

$(DEBUG_BUILD_DIR)/$(SRC_DIR)/%.o: $(SRC_DIR)/%.cpp
	@mkdir -p $(@D)
	$(CXX) $(DEBUG_CXXFLAGS) $(IFLAGS) -MMD -MP -c $< -o $@

# $(BUILD_DIR)/$(SRC_DIR)/%.bonus.o: $(SRC_DIR)/%.cpp
# 	@mkdir -p $(@D)
# 	$(CXX) $(CXXFLAGS) $(IFLAGS) -MMD -MP -c -DBONUS $< -o $@

# $(DEBUG_BUILD_DIR)/$(SRC_DIR)/%.bonus.o: $(SRC_DIR)/%.cpp
# 	@mkdir -p $(@D)
# 	$(CXX) $(DEBUG_CXXFLAGS) $(IFLAGS) -MMD -MP -c -DBONUS $< -o $@

# ---- Cleanup -----------------------------------------------------------------
clean:
	rm -rf $(BUILD_DIR) $(DEBUG_BUILD_DIR)

fclean: clean
	rm -f $(NAME) $(DEBUG_NAME) $(BONUS) $(DEBUG_BONUS)

re: fclean all

-include $(DEPS)
# -include $(DEPS) $(BONUS_DEPS)

.PHONY: all bonus clean fclean re debug debug_bonus format tidy tidy-all
