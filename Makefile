# ---- Library and Project names -----------------------------------------------
NAME			= ircserv
# BONUS			= ircserv_bonus

# ---- Sources & Objects -------------------------------------------------------
SRC_DIR			= srcs
INC_DIR			= includes
BUILD_DIR		= build

SRCS_ROOT		= main

SRCS_SERVER 	= IRCServer
# 				  Client \
# 				  Channel

# SRCS_COMMANDS	= Command \
# 				  Pass \
# 				  Nick \
# 				  User \
# 				  Quit \
# 				  Join \
# 				  Kick \
# 				  Invite \
# 				  Topic \
# 				  Mode \
# 				  Privmsg \
# 				  Notice \
# 				  Ping

# SRCS_UTILS		= Parser \
# 				  Reply

# SRCS_BONUS		= DCC \
# 				  Bot

SRCS			= $(SRCS_ROOT) \
 				  $(addprefix server/, $(SRCS_SERVER))

OBJS			= $(addprefix $(BUILD_DIR)/$(SRC_DIR)/, $(addsuffix .o, $(SRCS)))

# BONUS_OBJS	= $(OBJS:.o=.bonus.o)

# ---- Auto-generated header deps ----------------------------------------------

DEPS			= $(OBJS:.o=.d)
# BONUS_DEPS	= $(BONUS_OBJS:.o=.d)

# ---- Compiler and Flags ------------------------------------------------------

CXX				= c++
CXXFLAGS		= -Wall -Wextra -Werror -pedantic -std=c++98
IFLAGS			= -I$(INC_DIR)
DFLAGS			= -DDEBUG -g -fsanitize=address

# ---- Rules -------------------------------------------------------------------

all: $(NAME)

# bonus: $(BONUS)

debug: CXXFLAGS += $(DFLAGS)
debug: $(NAME)

# debug_bonus: CXXFLAGS += $(DFLAGS)
# debug_bonus: $(BONUS)

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

$(NAME): $(OBJS)
	$(CXX) $(CXXFLAGS) $(OBJS) -o $@

# $(BONUS): $(BONUS_OBJS)
# 	$(CXX) $(CXXFLAGS) $(BONUS_OBJS) -o $@

$(BUILD_DIR)/$(SRC_DIR)/%.o: $(SRC_DIR)/%.cpp
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) $(IFLAGS) -MMD -MP -c $< -o $@

# $(BUILD_DIR)/$(SRC_DIR)/%.bonus.o: $(SRC_DIR)/%.cpp
# 	@mkdir -p $(@D)
# 	$(CXX) $(CXXFLAGS) $(IFLAGS) -MMD -MP -c -DBONUS $< -o $@

# ---- Cleanup -----------------------------------------------------------------
clean:
	rm -rf $(BUILD_DIR)

fclean: clean
	rm -f $(NAME) $(BONUS)

re: fclean all

-include $(DEPS)
# -include $(DEPS) $(BONUS_DEPS)

.PHONY: all bonus clean fclean re debug debug_bonus format tidy tidy-all
