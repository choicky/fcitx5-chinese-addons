configure_file(testdir.h.in ${CMAKE_CURRENT_BINARY_DIR}/testdir.h @ONLY)
include_directories(${CMAKE_CURRENT_BINARY_DIR})

add_executable(testpunctuation testpunctuation.cpp)
target_link_libraries(testpunctuation Fcitx5::Core Fcitx5::Module::Punctuation)
add_dependencies(testpunctuation punctuation punctuation.conf.in-fmt)
add_test(NAME testpunctuation COMMAND testpunctuation)

if (ENABLE_CLOUDPINYIN)
add_executable(testcloudpinyinbackend testcloudpinyinbackend.cpp ../modules/cloudpinyin/backend.cpp)
target_include_directories(testcloudpinyinbackend PRIVATE ${PROJECT_SOURCE_DIR}/modules/cloudpinyin PkgConfig::Curl)
target_link_libraries(testcloudpinyinbackend Fcitx5::Core nlohmann_json::nlohmann_json PkgConfig::Curl)
add_test(NAME testcloudpinyinbackend COMMAND testcloudpinyinbackend)

add_executable(testcloudpinyin testcloudpinyin.cpp)
target_link_libraries(testcloudpinyin
    Fcitx5::Core Fcitx5::Config Fcitx5::Module::CloudPinyin)
add_dependencies(testcloudpinyin cloudpinyin copy-addon-cloudpinyin)
endif()

add_executable(testpinyinhelper testpinyinhelper.cpp)
target_link_libraries(testpinyinhelper Fcitx5::Core Fcitx5::Module::PinyinHelper)
add_dependencies(testpinyinhelper pinyin pinyin.conf.in-fmt pinyinhelper pinyinhelper.conf.in-fmt)
add_test(NAME testpinyinhelper COMMAND testpinyinhelper)

add_executable(testfullwidth testfullwidth.cpp)
target_link_libraries(testfullwidth Fcitx5::Core Fcitx5::Module::TestFrontend Fcitx5::Module::TestIM)
add_dependencies(testfullwidth fullwidth fullwidth.conf.in-fmt)
add_test(NAME testfullwidth COMMAND testfullwidth)

add_subdirectory(inputmethod)
add_executable(testchttrans testchttrans.cpp)
target_link_libraries(testchttrans Fcitx5::Core Fcitx5::Module::TestFrontend Fcitx5::Module::TestIM)
add_dependencies(testchttrans chttrans chttrans.conf.in-fmt copy-addon copy-testim)
add_test(NAME testchttrans COMMAND testchttrans)

add_subdirectory(addon)
add_executable(testpinyin testpinyin.cpp)
target_link_libraries(testpinyin Fcitx5::Core Fcitx5::Module::TestFrontend)
add_dependencies(testpinyin pinyin pinyinhelper copy-addon copy-im)
add_test(NAME testpinyin COMMAND testpinyin)
add_executable(testtable testtable.cpp)
target_link_libraries(testtable Fcitx5::Core Fcitx5::Module::TestFrontend)
add_dependencies(testtable table copy-addon copy-im)
add_test(NAME testtable COMMAND testtable)

add_executable(testcustomphrase testcustomphrase.cpp ../im/pinyin/customphrase.cpp)
target_compile_definitions(testcustomphrase PRIVATE "-DFCITX_CUSTOM_PHRASE_TEST")
target_link_libraries(testcustomphrase Fcitx5::Utils LibIME::Core)
add_test(NAME testcustomphrase COMMAND testcustomphrase)

add_executable(testsymboldictionary testsymboldictionary.cpp ../im/pinyin/symboldictionary.cpp)
target_link_libraries(testsymboldictionary Fcitx5::Utils LibIME::Core)
add_test(NAME testsymboldictionary COMMAND testsymboldictionary)

add_executable(testmixedcompositionstate testmixedcompositionstate.cpp
    ../im/pinyin/mixed/mixedcompositionstate.cpp)
target_include_directories(testmixedcompositionstate PRIVATE
    ${PROJECT_SOURCE_DIR}/im/pinyin ${PROJECT_SOURCE_DIR}/im/pinyin/mixed)
add_test(NAME testmixedcompositionstate COMMAND testmixedcompositionstate)

add_executable(testmixedsegmentation testmixedsegmentation.cpp
    ../im/pinyin/mixed/mixedsegmentation.cpp)
target_include_directories(testmixedsegmentation PRIVATE
    ${PROJECT_SOURCE_DIR}/im/pinyin ${PROJECT_SOURCE_DIR}/im/pinyin/mixed)
add_test(NAME testmixedsegmentation COMMAND testmixedsegmentation)

add_executable(testenglishlexicon testenglishlexicon.cpp
    ../im/pinyin/mixed/english/englishlexicon.cpp
    ../im/pinyin/mixed/english/englisharcoracle.cpp
    ../im/pinyin/mixed/mixedsegmentation.cpp)
target_include_directories(testenglishlexicon PRIVATE
    ${PROJECT_SOURCE_DIR}/im/pinyin ${PROJECT_SOURCE_DIR}/im/pinyin/mixed
    ${PROJECT_SOURCE_DIR}/im/pinyin/mixed/english)
add_test(NAME testenglishlexicon COMMAND testenglishlexicon)

add_executable(testenglishuserlexicon testenglishuserlexicon.cpp
    ../im/pinyin/mixed/english/englishuserlexicon.cpp
    ../im/pinyin/mixed/mixedsegmentation.cpp)
target_include_directories(testenglishuserlexicon PRIVATE
    ${PROJECT_SOURCE_DIR}/im/pinyin ${PROJECT_SOURCE_DIR}/im/pinyin/mixed
    ${PROJECT_SOURCE_DIR}/im/pinyin/mixed/english)
add_test(NAME testenglishuserlexicon COMMAND testenglishuserlexicon)

add_executable(testenglishcorrection testenglishcorrection.cpp
    ../im/pinyin/mixed/english/englishlexicon.cpp
    ../im/pinyin/mixed/english/englishcorrectionoracle.cpp
    ../im/pinyin/mixed/mixedsegmentation.cpp)
target_include_directories(testenglishcorrection PRIVATE
    ${PROJECT_SOURCE_DIR}/im/pinyin ${PROJECT_SOURCE_DIR}/im/pinyin/mixed
    ${PROJECT_SOURCE_DIR}/im/pinyin/mixed/english)
add_test(NAME testenglishcorrection COMMAND testenglishcorrection)

add_executable(testmixedpipeline testmixedpipeline.cpp
    ../im/pinyin/mixed/mixedcompositionstate.cpp
    ../im/pinyin/mixed/mixedsegmentation.cpp
    ../im/pinyin/mixed/segmentcomposer.cpp
    ../im/pinyin/mixed/unifiedranker.cpp
    ../im/pinyin/mixed/rewriter.cpp
    ../im/pinyin/mixed/english/englishlexicon.cpp
    ../im/pinyin/mixed/english/englisharcoracle.cpp
    ../im/pinyin/mixed/english/englishuserlexicon.cpp
    ../im/pinyin/mixed/english/englishcorrectionoracle.cpp)
target_include_directories(testmixedpipeline PRIVATE
    ${PROJECT_SOURCE_DIR}/im/pinyin ${PROJECT_SOURCE_DIR}/im/pinyin/mixed
    ${PROJECT_SOURCE_DIR}/im/pinyin/mixed/english)
add_test(NAME testmixedpipeline COMMAND testmixedpipeline)

add_executable(testmixedengine testmixedengine.cpp
    ../im/pinyin/mixed/mixedcompositionstate.cpp
    ../im/pinyin/mixed/mixedsegmentation.cpp
    ../im/pinyin/mixed/segmentcomposer.cpp
    ../im/pinyin/mixed/unifiedranker.cpp
    ../im/pinyin/mixed/rewriter.cpp
    ../im/pinyin/mixed/mixedengine.cpp
    ../im/pinyin/mixed/english/englishlexicon.cpp
    ../im/pinyin/mixed/english/englisharcoracle.cpp
    ../im/pinyin/mixed/english/englishuserlexicon.cpp
    ../im/pinyin/mixed/english/englishcorrectionoracle.cpp)
target_include_directories(testmixedengine PRIVATE
    ${PROJECT_SOURCE_DIR}/im/pinyin ${PROJECT_SOURCE_DIR}/im/pinyin/mixed
    ${PROJECT_SOURCE_DIR}/im/pinyin/mixed/english)
add_test(NAME testmixedengine COMMAND testmixedengine)

# Batch 8 — Chinese correction baseline/after harness. Drives the real
# LibIME-backed `LibIMEChineseArcOracle` with a `PinyinCorrectionProfile`
# to measure correction recall + provenance + false-correction guards
# before and after enabling the correction profile at the oracle seam.
add_executable(testmixedchinesecorrection testmixedchinesecorrection.cpp
    ../im/pinyin/mixed/chinesearcoracle.cpp
    ../im/pinyin/mixed/mixedcompositionstate.cpp
    ../im/pinyin/mixed/mixedsegmentation.cpp)
target_link_libraries(testmixedchinesecorrection PRIVATE
    Fcitx5::Utils LibIME::Pinyin LibIME::Core)
target_include_directories(testmixedchinesecorrection PRIVATE
    ${PROJECT_SOURCE_DIR}/im/pinyin ${PROJECT_SOURCE_DIR}/im/pinyin/mixed)
add_test(NAME testmixedchinesecorrection COMMAND testmixedchinesecorrection)

# Batch 10 — Formal mixed-input corpus + accuracy + performance gate. Drives
# the Architecture A orchestration (segmentation search -> compose -> pool ->
# ranker -> rewriter) against a curated corpus and asserts top-1 accuracy,
# top-K recall, false-correction guards, latency ceiling and 200-iteration
# stability of the top-1 composed text (no engine-side state drift).
add_executable(testmixedcorpus testmixedcorpus.cpp
    ../im/pinyin/mixed/mixedcompositionstate.cpp
    ../im/pinyin/mixed/mixedsegmentation.cpp
    ../im/pinyin/mixed/segmentcomposer.cpp
    ../im/pinyin/mixed/unifiedranker.cpp
    ../im/pinyin/mixed/rewriter.cpp
    ../im/pinyin/mixed/mixedengine.cpp
    ../im/pinyin/mixed/english/englishlexicon.cpp
    ../im/pinyin/mixed/english/englisharcoracle.cpp
    ../im/pinyin/mixed/english/englishuserlexicon.cpp
    ../im/pinyin/mixed/english/englishcorrectionoracle.cpp)
target_include_directories(testmixedcorpus PRIVATE
    ${PROJECT_SOURCE_DIR}/im/pinyin ${PROJECT_SOURCE_DIR}/im/pinyin/mixed
    ${PROJECT_SOURCE_DIR}/im/pinyin/mixed/english)
add_test(NAME testmixedcorpus COMMAND testmixedcorpus)

# Batch 10 — Shuangpin oracle smoke test (regression matrix). Drives the real
# LibIME-backed `LibIMEChineseArcOracle` in Shuangpin mode with a builtin
# Ziranma `ShuangpinProfile` to verify the graph builder + arc oracle path
# still works, and asserts the recorded §17.2 Case D ceiling: setting a
# `PinyinCorrectionProfile` on a Shuangpin-mode oracle does not enable
# correction (Shuangpin correction is baked into the `ShuangpinProfile` key
# map at construction, no per-arc provenance is observable through the public
# LibIME API).
add_executable(testmixedshuangpin testmixedshuangpin.cpp
    ../im/pinyin/mixed/chinesearcoracle.cpp
    ../im/pinyin/mixed/mixedcompositionstate.cpp
    ../im/pinyin/mixed/mixedsegmentation.cpp)
target_link_libraries(testmixedshuangpin PRIVATE
    Fcitx5::Utils LibIME::Pinyin LibIME::Core)
target_include_directories(testmixedshuangpin PRIVATE
    ${PROJECT_SOURCE_DIR}/im/pinyin ${PROJECT_SOURCE_DIR}/im/pinyin/mixed)
add_test(NAME testmixedshuangpin COMMAND testmixedshuangpin)
