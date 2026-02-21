include(CheckCXXSourceCompiles)

if(MSVC)
  set(CMAKE_REQUIRED_FLAGS "${CMAKE_REQUIRED_FLAGS} /std:c++20")
else()
  set(CMAKE_REQUIRED_FLAGS "${CMAKE_REQUIRED_FLAGS} -std=c++20")
endif()

set(CMAKE_REQUIRED_INCLUDES "${CMAKE_SOURCE_DIR}/include")

function(ba_expect_compile_failure test_name source_code)
  check_cxx_source_compiles("${source_code}" "${test_name}_COMPILES")
  if(${test_name}_COMPILES)
    message(FATAL_ERROR "[CompileTests] ${test_name} unexpectedly compiled.")
  else()
    message(STATUS "[CompileTests] ${test_name} correctly rejected.")
  endif()
endfunction()

ba_expect_compile_failure(
  bad_padding_type
  [[
    #include <cstdint>
    #include "big_atomics/big_atomic_me.hpp"
    struct BadPadding {
      std::uint8_t tag;
      std::uint64_t payload;
    };
    int main() {
      ba::BigAtomic<BadPadding> value;
      (void)value;
      return 0;
    }
  ]]
)

ba_expect_compile_failure(
  non_trivial_destructor
  [[
    #include <cstdint>
    #include "big_atomics/big_atomic_me.hpp"
    struct BadDestructor {
      std::uint64_t payload;
      ~BadDestructor() {}
    };
    int main() {
      ba::BigAtomic<BadDestructor> value;
      (void)value;
      return 0;
    }
  ]]
)

ba_expect_compile_failure(
  odd_sized_type
  [[
    #include <cstdint>
    #include "big_atomics/big_atomic_me.hpp"
    struct BadSize {
      std::uint32_t a;
      std::uint32_t b;
      std::uint16_t c;
    };
    int main() {
      ba::BigAtomic<BadSize> value;
      (void)value;
      return 0;
    }
  ]]
)
