#include <ClassFlowCNNGeneral.h>


class UnderTestCNNGeneral : public ClassFlowCNNGeneral
{
    public:
        UnderTestCNNGeneral( ClassFlowAlignment *_flowalign, CNNType _cnntype) :
            ClassFlowCNNGeneral(_flowalign, "name", _cnntype) {};

        using ClassFlowCNNGeneral::evalAnalogToDigitTransition;
};


/**
 * @brief
 *
 * Transition = x.8 - x.2 here no transition in the test cases.
 * Offset = dig=x.n, ana= n.y: no offset, because both "n" are the same
 */
void test_analogToDigit_Standard()
{
    UnderTestCNNGeneral* undertest = new UnderTestCNNGeneral(nullptr, CNNTYPE_DIGIT_CLASS100);

    // 4.8 is a "hanging" 5, i.e. it has not jumped over to 5.0.
    // A "hanging digit" should still be rounded from Transition.
    // Transition = yes
    // Offset = no
    TEST_ASSERT_EQUAL_INT(5,  undertest->evalAnalogToDigitTransition(48, 80, 8, 92));

    // https://github.com/jomjol/AI-on-the-edge-device/issues/921#issue-1344032217
    // Default: dig=9.6, ana=6.8 => erg=9
    // Transition = no
    // Offset = no
    TEST_ASSERT_EQUAL_INT(9,  undertest->evalAnalogToDigitTransition(96, 68, 6, 92));


    // https://github.com/jomjol/AI-on-the-edge-device/issues/921#issuecomment-1220365920
    // Default: dig=4.6, ana=6.2 => erg=4
    // Transition = no
    // Offset = no
    TEST_ASSERT_EQUAL_INT(4,  undertest->evalAnalogToDigitTransition(46, 62, 6, 92));

    // https://github.com/jomjol/AI-on-the-edge-device/issues/1143#issuecomment-1274434805
    // Hanging digit ()
    // Default: dig=6.8, ana=8.6 => erg=7
    // Transition = no
    // Offset = no
    TEST_ASSERT_EQUAL_INT(7,  undertest->evalAnalogToDigitTransition(68, 86, 6, 92));

    // https://github.com/jomjol/AI-on-the-edge-device/issues/1143#issuecomment-1274434805
    // Also hanging digit () with small pointer after 0 pass.
    // Default: dig=6.8, ana=1.0 => erg=7
    // Transition = no
    // Offset = no
    TEST_ASSERT_EQUAL_INT(7,  undertest->evalAnalogToDigitTransition(68, 10, 1, 92));
}


void test_analogToDigit_Transition()
{
    UnderTestCNNGeneral* undertest = new UnderTestCNNGeneral(nullptr, CNNTYPE_DIGIT_CLASS100);

    // https://github.com/jomjol/AI-on-the-edge-device/issues/921#issuecomment-1222672175
    // Default: dig=3.9, ana=9.7 => erg=3
    // Transition = yes
    // Zero crossing = no
    // Offset = no
    TEST_ASSERT_EQUAL_INT(3,  undertest->evalAnalogToDigitTransition(39, 97, 9, 92));

    // without reference
    // Default: dig=4.0, ana=9.1 => erg=4
    // Transition = yes
    // Zero crossing = no
    // Offset = no
    // Special feature: Digit has not yet started at analogue 9.1
    TEST_ASSERT_EQUAL_INT(4,  undertest->evalAnalogToDigitTransition(40, 91, 9, 92));

    // without reference
    // Default: dig=9.8, ana=0.1, ana_2=9.9 => erg=9
    // transition = yes
    // Zero crossing = no
    // Offset = no
    // Special feature: analogue is set back to 9 by previous analogue
    TEST_ASSERT_EQUAL_INT(9,  undertest->evalAnalogToDigitTransition(98, 01, 9, 92));


    // https://github.com/jomjol/AI-on-the-edge-device/issues/1110#issuecomment-1277425333
    // Default: dig=5.9, ana=9.4 => erg=9
    // Transition = yes
    // Zero crossing = no
    // Offset = no
    // Special feature:
    TEST_ASSERT_EQUAL_INT(5,  undertest->evalAnalogToDigitTransition(59, 94, 9, 92));

    // https://github.com/jomjol/AI-on-the-edge-device/issues/1110#issuecomment-1282168030
    // Default: dig=1.8, ana=7.8 => erg=9
    // Transition = yes
    // Zero crossing = no
    // Offset = no
    // Special feature: Digit runs with analogue. Therefore 1.8 (vs. 7.8)
    TEST_ASSERT_EQUAL_INT(1,  undertest->evalAnalogToDigitTransition(18, 78, 7, 7.7));
}
