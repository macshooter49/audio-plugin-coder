// JUCE's Time::getCompilationDate() wants these two globals, normally emitted by the JUCE
// CMake target. A standalone cert TU has to supply them.
namespace juce { extern const char* const juce_compilationDate; extern const char* const juce_compilationTime;
                 const char* const juce_compilationDate = __DATE__;
                 const char* const juce_compilationTime = __TIME__; }
