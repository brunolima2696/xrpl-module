#include <test/app/lending/LoanTestBase.h>
#include <test/jtx/Account.h>
#include <test/jtx/Env.h>
#include <test/jtx/TestHelpers.h>
#include <test/jtx/amount.h>
#include <test/jtx/credentials.h>
#include <test/jtx/fee.h>
#include <test/jtx/flags.h>
#include <test/jtx/mpt.h>
#include <test/jtx/multisign.h>
#include <test/jtx/noop.h>
#include <test/jtx/pay.h>
#include <test/jtx/permissioned_domains.h>
#include <test/jtx/trust.h>
#include <test/jtx/vault.h>

#include <xrpl/basics/Number.h>
#include <xrpl/basics/chrono.h>
#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/json/json_value.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/Issue.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/Protocol.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/protocol/Units.h>
#include <xrpl/protocol/XRPAmount.h>
#include <xrpl/tx/transactors/lending/LoanSet.h>

#include <array>
#include <chrono>
#include <cstdint>
#include <functional>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

namespace xrpl::test {

class LoanSet_test : public LoanTestBase
{
private:
    void
    testLoanSet(FeatureBitset features)
    {
        using namespace jtx;

        Account const issuer{"issuer"};
        Account const lender{"lender"};
        Account const borrower{"borrower"};

        struct CaseArgs
        {
            bool requireAuth = false;
            bool authorizeBorrower = false;
            int initialXRP = 1'000'000;
        };

        auto const testCase = [&, this](
                                  std::function<void(Env&, BrokerInfo const&, MPTTester&)> mptTest,
                                  std::function<void(Env&, BrokerInfo const&)> iouTest,
                                  CaseArgs args = {}) {
            Env env(*this, features);
            env.fund(XRP(args.initialXRP), issuer, lender, borrower);
            env.close();
            if (args.requireAuth)
            {
                env(fset(issuer, asfRequireAuth));
                env.close();
            }

            // We need two different asset types, MPT and IOU. Prepare MPT
            // first
            MPTTester mptt{env, issuer, kMptInitNoFund};

            auto const kNone = LedgerSpecificFlags(0);
            mptt.create(
                {.flags = tfMPTCanTransfer | tfMPTCanLock |
                     (args.requireAuth ? tfMPTRequireAuth : kNone)});
            env.close();
            PrettyAsset const mptAsset = mptt.issuanceID();
            mptt.authorize({.account = lender});
            mptt.authorize({.account = borrower});
            env.close();
            if (args.requireAuth)
            {
                mptt.authorize({.account = issuer, .holder = lender});
                if (args.authorizeBorrower)
                    mptt.authorize({.account = issuer, .holder = borrower});
                env.close();
            }

            env(pay(issuer, lender, mptAsset(10'000'000)));
            env.close();

            // Prepare IOU
            PrettyAsset const iouAsset = issuer[iouCurrency_];
            env(trust(lender, iouAsset(10'000'000)));
            env(trust(borrower, iouAsset(10'000'000)));
            env.close();
            if (args.requireAuth)
            {
                env(trust(issuer, iouAsset(0), lender, tfSetfAuth));
                env(pay(issuer, lender, iouAsset(10'000'000)));
                if (args.authorizeBorrower)
                {
                    env(trust(issuer, iouAsset(0), borrower, tfSetfAuth));
                    env(pay(issuer, borrower, iouAsset(10'000)));
                }
            }
            else
            {
                env(pay(issuer, lender, iouAsset(10'000'000)));
                env(pay(issuer, borrower, iouAsset(10'000)));
            }
            env.close();

            // Create vaults and loan brokers
            std::array const assets{mptAsset, iouAsset};
            std::vector<BrokerInfo> brokers;
            brokers.reserve(assets.size());
            for (auto const& asset : assets)
            {
                brokers.emplace_back(createVaultAndBroker(env, asset, lender));
            }

            if (mptTest)
                mptTest(env, brokers[0], mptt);
            if (iouTest)
                iouTest(env, brokers[1]);
        };

        testCase(
            [&, this](Env& env, BrokerInfo const& broker, auto&) {
                using namespace loan;
                Number const principalRequest = broker.asset(1'000).value();

                testcase("MPT issuer is borrower, issuer submits");
                env(set(issuer, broker.brokerID, principalRequest),
                    kCounterparty(lender),
                    Sig(sfCounterpartySignature, lender),
                    Fee(env.current()->fees().base * 5));

                testcase("MPT issuer is borrower, lender submits");
                env(set(lender, broker.brokerID, principalRequest),
                    kCounterparty(issuer),
                    Sig(sfCounterpartySignature, issuer),
                    Fee(env.current()->fees().base * 5));
            },
            [&, this](Env& env, BrokerInfo const& broker) {
                using namespace loan;
                Number const principalRequest = broker.asset(1'000).value();

                testcase("IOU issuer is borrower, issuer submits");
                env(set(issuer, broker.brokerID, principalRequest),
                    kCounterparty(lender),
                    Sig(sfCounterpartySignature, lender),
                    Fee(env.current()->fees().base * 5));

                testcase("IOU issuer is borrower, lender submits");
                env(set(lender, broker.brokerID, principalRequest),
                    kCounterparty(issuer),
                    Sig(sfCounterpartySignature, issuer),
                    Fee(env.current()->fees().base * 5));
            },
            CaseArgs{.requireAuth = true});

        testCase(
            [&, this](Env& env, BrokerInfo const& broker, auto&) {
                using namespace loan;
                Number const principalRequest = broker.asset(1'000).value();

                testcase("MPT unauthorized borrower, borrower submits");
                env(set(borrower, broker.brokerID, principalRequest),
                    kCounterparty(lender),
                    Sig(sfCounterpartySignature, lender),
                    Fee(env.current()->fees().base * 5),
                    Ter{tecNO_AUTH});

                testcase("MPT unauthorized borrower, lender submits");
                env(set(lender, broker.brokerID, principalRequest),
                    kCounterparty(borrower),
                    Sig(sfCounterpartySignature, borrower),
                    Fee(env.current()->fees().base * 5),
                    Ter{tecNO_AUTH});
            },
            [&, this](Env& env, BrokerInfo const& broker) {
                using namespace loan;
                Number const principalRequest = broker.asset(1'000).value();

                testcase("IOU unauthorized borrower, borrower submits");
                env(set(borrower, broker.brokerID, principalRequest),
                    kCounterparty(lender),
                    Sig(sfCounterpartySignature, lender),
                    Fee(env.current()->fees().base * 5),
                    Ter{tecNO_AUTH});

                testcase("IOU unauthorized borrower, lender submits");
                env(set(lender, broker.brokerID, principalRequest),
                    kCounterparty(borrower),
                    Sig(sfCounterpartySignature, borrower),
                    Fee(env.current()->fees().base * 5),
                    Ter{tecNO_AUTH});
            },
            CaseArgs{.requireAuth = true});

        auto const [acctReserve, incReserve] = [this]() -> std::pair<int, int> {
            Env const env{*this, testableAmendments()};
            return {
                env.current()->fees().accountReserve(0, 1).drops() / kDropsPerXrp.drops(),
                env.current()->fees().increment.drops() / kDropsPerXrp.drops()};
        }();

        testCase(
            [&, this](Env& env, BrokerInfo const& broker, MPTTester& mptt) {
                using namespace loan;
                Number const principalRequest = broker.asset(1'000).value();

                testcase(
                    "MPT authorized borrower, borrower submits, borrower has "
                    "no reserve");
                mptt.authorize({.account = borrower, .flags = tfMPTUnauthorize});
                env.close();

                auto const mptoken = keylet::mptoken(mptt.issuanceID(), borrower);
                auto const sleMPT1 = env.le(mptoken);
                BEAST_EXPECT(sleMPT1 == nullptr);

                // Burn some XRP
                env(noop(borrower), Fee(XRP((acctReserve * 2) + (incReserve * 2))));
                env.close();

                // Cannot create loan, not enough reserve to create MPToken
                env(set(borrower, broker.brokerID, principalRequest),
                    kCounterparty(lender),
                    Sig(sfCounterpartySignature, lender),
                    Fee(env.current()->fees().base * 5),
                    Ter{tecINSUFFICIENT_RESERVE});
                env.close();

                // Can create loan now, will implicitly create MPToken
                env(pay(issuer, borrower, XRP(incReserve)));
                env.close();
                env(set(borrower, broker.brokerID, principalRequest),
                    kCounterparty(lender),
                    Sig(sfCounterpartySignature, lender),
                    Fee(env.current()->fees().base * 5));
                env.close();

                auto const sleMPT2 = env.le(mptoken);
                BEAST_EXPECT(sleMPT2 != nullptr);
            },
            {},
            CaseArgs{.initialXRP = (acctReserve * 2) + (incReserve * 8) + 1});

        testCase(
            {},
            [&, this](Env& env, BrokerInfo const& broker) {
                using namespace loan;
                Number const principalRequest = broker.asset(1'000).value();

                testcase(
                    "IOU authorized borrower, borrower submits, borrower has "
                    "no reserve");
                // Remove trust line from borrower to issuer
                env.trust(broker.asset(0), borrower);
                env.close();

                env(pay(borrower, issuer, broker.asset(10'000)));
                env.close();
                auto const trustline = keylet::trustLine(borrower, broker.asset.raw().get<Issue>());
                auto const sleLine1 = env.le(trustline);
                BEAST_EXPECT(sleLine1 == nullptr);

                // Burn some XRP
                env(noop(borrower), Fee(XRP((acctReserve * 2) + (incReserve * 2))));
                env.close();

                // Cannot create loan, not enough reserve to create trust line
                env(set(borrower, broker.brokerID, principalRequest),
                    kCounterparty(lender),
                    Sig(sfCounterpartySignature, lender),
                    Fee(env.current()->fees().base * 5),
                    Ter{tecNO_LINE_INSUF_RESERVE});
                env.close();

                // Can create loan now, will implicitly create trust line
                env(pay(issuer, borrower, XRP(incReserve)));
                env.close();
                env(set(borrower, broker.brokerID, principalRequest),
                    kCounterparty(lender),
                    Sig(sfCounterpartySignature, lender),
                    Fee(env.current()->fees().base * 5));
                env.close();

                auto const sleLine2 = env.le(trustline);
                BEAST_EXPECT(sleLine2 != nullptr);
            },
            CaseArgs{.initialXRP = (acctReserve * 2) + (incReserve * 8) + 1});

        testCase(
            [&, this](Env& env, BrokerInfo const& broker, MPTTester& mptt) {
                using namespace loan;
                Number const principalRequest = broker.asset(1'000).value();

                testcase(
                    "MPT authorized borrower, borrower submits, lender has "
                    "no reserve");
                auto const mptoken = keylet::mptoken(mptt.issuanceID(), lender);
                auto const sleMPT1 = env.le(mptoken);
                BEAST_EXPECT(sleMPT1 != nullptr);

                env(pay(lender, issuer, broker.asset(sleMPT1->at(sfMPTAmount))));
                env.close();

                mptt.authorize({.account = lender, .flags = tfMPTUnauthorize});
                env.close();

                auto const sleMPT2 = env.le(mptoken);
                BEAST_EXPECT(sleMPT2 == nullptr);

                // Burn some XRP
                env(noop(lender), Fee(XRP(incReserve)));
                env.close();

                // Cannot create loan, not enough reserve to create MPToken
                env(set(borrower, broker.brokerID, principalRequest),
                    kLoanOriginationFee(broker.asset(1).value()),
                    kCounterparty(lender),
                    Sig(sfCounterpartySignature, lender),
                    Fee(env.current()->fees().base * 5),
                    Ter{tecINSUFFICIENT_RESERVE});
                env.close();

                // Can create loan now, will implicitly create MPToken
                env(pay(issuer, lender, XRP(incReserve)));
                env.close();
                env(set(borrower, broker.brokerID, principalRequest),
                    kLoanOriginationFee(broker.asset(1).value()),
                    kCounterparty(lender),
                    Sig(sfCounterpartySignature, lender),
                    Fee(env.current()->fees().base * 5));
                env.close();

                auto const sleMPT3 = env.le(mptoken);
                BEAST_EXPECT(sleMPT3 != nullptr);
            },
            {},
            CaseArgs{.initialXRP = (acctReserve * 2) + (incReserve * 8) + 1});

        testCase(
            {},
            [&, this](Env& env, BrokerInfo const& broker) {
                using namespace loan;
                Number const principalRequest = broker.asset(1'000).value();

                testcase(
                    "IOU authorized borrower, borrower submits, lender has no "
                    "reserve");
                // Remove trust line from lender to issuer
                env.trust(broker.asset(0), lender);
                env.close();

                auto const trustline = keylet::trustLine(lender, broker.asset.raw().get<Issue>());
                auto const sleLine1 = env.le(trustline);
                BEAST_EXPECT(sleLine1 != nullptr);

                env(pay(lender, issuer, broker.asset(abs(sleLine1->at(sfBalance).value()))));
                env.close();
                auto const sleLine2 = env.le(trustline);
                BEAST_EXPECT(sleLine2 == nullptr);

                // Burn some XRP
                env(noop(lender), Fee(XRP(incReserve)));
                env.close();

                // Cannot create loan, not enough reserve to create trust line
                env(set(borrower, broker.brokerID, principalRequest),
                    kLoanOriginationFee(broker.asset(1).value()),
                    kCounterparty(lender),
                    Sig(sfCounterpartySignature, lender),
                    Fee(env.current()->fees().base * 5),
                    Ter{tecNO_LINE_INSUF_RESERVE});
                env.close();

                // Can create loan now, will implicitly create trust line
                env(pay(issuer, lender, XRP(incReserve)));
                env.close();
                env(set(borrower, broker.brokerID, principalRequest),
                    kLoanOriginationFee(broker.asset(1).value()),
                    kCounterparty(lender),
                    Sig(sfCounterpartySignature, lender),
                    Fee(env.current()->fees().base * 5));
                env.close();

                auto const sleLine3 = env.le(trustline);
                BEAST_EXPECT(sleLine3 != nullptr);
            },
            CaseArgs{.initialXRP = (acctReserve * 2) + (incReserve * 8) + 1});

        testCase(
            [&, this](Env& env, BrokerInfo const& broker, MPTTester& mptt) {
                using namespace loan;
                Number const principalRequest = broker.asset(1'000).value();

                testcase("MPT authorized borrower, unauthorized lender");
                auto const mptoken = keylet::mptoken(mptt.issuanceID(), lender);
                auto const sleMPT1 = env.le(mptoken);
                BEAST_EXPECT(sleMPT1 != nullptr);

                env(pay(lender, issuer, broker.asset(sleMPT1->at(sfMPTAmount))));
                env.close();

                mptt.authorize({.account = lender, .flags = tfMPTUnauthorize});
                env.close();

                auto const sleMPT2 = env.le(mptoken);
                BEAST_EXPECT(sleMPT2 == nullptr);

                // Cannot create loan, lender not authorized to receive fee
                env(set(borrower, broker.brokerID, principalRequest),
                    kLoanOriginationFee(broker.asset(1).value()),
                    kCounterparty(lender),
                    Sig(sfCounterpartySignature, lender),
                    Fee(env.current()->fees().base * 5),
                    Ter{tecNO_AUTH});
                env.close();

                // Cannot create loan, even without an origination fee
                env(set(borrower, broker.brokerID, principalRequest),
                    kCounterparty(lender),
                    Sig(sfCounterpartySignature, lender),
                    Fee(env.current()->fees().base * 5),
                    Ter{tecNO_AUTH});
                env.close();

                // No MPToken for lender - no authorization and no payment
                auto const sleMPT3 = env.le(mptoken);
                BEAST_EXPECT(sleMPT3 == nullptr);
            },
            {},
            CaseArgs{.requireAuth = true, .authorizeBorrower = true});

        testCase(
            [&, this](Env& env, BrokerInfo const& broker, auto&) {
                using namespace loan;
                Number const principalRequest = broker.asset(1'000).value();

                testcase("MPT authorized borrower, borrower submits");
                env(set(borrower, broker.brokerID, principalRequest),
                    kCounterparty(lender),
                    Sig(sfCounterpartySignature, lender),
                    Fee(env.current()->fees().base * 5));
            },
            [&, this](Env& env, BrokerInfo const& broker) {
                using namespace loan;
                Number const principalRequest = broker.asset(1'000).value();

                testcase("IOU authorized borrower, borrower submits");
                env(set(borrower, broker.brokerID, principalRequest),
                    kCounterparty(lender),
                    Sig(sfCounterpartySignature, lender),
                    Fee(env.current()->fees().base * 5));
            },
            CaseArgs{.requireAuth = true, .authorizeBorrower = true});

        testCase(
            [&, this](Env& env, BrokerInfo const& broker, auto&) {
                using namespace loan;
                Number const principalRequest = broker.asset(1'000).value();

                testcase("MPT authorized borrower, lender submits");
                env(set(lender, broker.brokerID, principalRequest),
                    kCounterparty(borrower),
                    Sig(sfCounterpartySignature, borrower),
                    Fee(env.current()->fees().base * 5));
            },
            [&, this](Env& env, BrokerInfo const& broker) {
                using namespace loan;
                Number const principalRequest = broker.asset(1'000).value();

                testcase("IOU authorized borrower, lender submits");
                env(set(lender, broker.brokerID, principalRequest),
                    kCounterparty(borrower),
                    Sig(sfCounterpartySignature, borrower),
                    Fee(env.current()->fees().base * 5));
            },
            CaseArgs{.requireAuth = true, .authorizeBorrower = true});

        jtx::Account const alice{"alice"};
        jtx::Account const bella{"bella"};
        auto const msigSetup = [&](Env& env, Account const& account) {
            json::Value const tx1 = signers(account, 2, {{alice, 1}, {bella, 1}});
            env(tx1);
            env.close();
        };

        testCase(
            [&, this](Env& env, BrokerInfo const& broker, auto&) {
                using namespace loan;
                msigSetup(env, lender);
                Number const principalRequest = broker.asset(1'000).value();

                testcase(
                    "MPT authorized borrower, borrower submits, lender "
                    "multisign");
                env(set(borrower, broker.brokerID, principalRequest),
                    kCounterparty(lender),
                    Msig(sfCounterpartySignature, alice, bella),
                    Fee(env.current()->fees().base * 5));
            },
            [&, this](Env& env, BrokerInfo const& broker) {
                using namespace loan;
                msigSetup(env, lender);
                Number const principalRequest = broker.asset(1'000).value();

                testcase(
                    "IOU authorized borrower, borrower submits, lender "
                    "multisign");
                env(set(borrower, broker.brokerID, principalRequest),
                    kCounterparty(lender),
                    Msig(sfCounterpartySignature, alice, bella),
                    Fee(env.current()->fees().base * 5));
            },
            CaseArgs{.requireAuth = true, .authorizeBorrower = true});

        testCase(
            [&, this](Env& env, BrokerInfo const& broker, auto&) {
                using namespace loan;
                msigSetup(env, borrower);
                Number const principalRequest = broker.asset(1'000).value();

                testcase(
                    "MPT authorized borrower, lender submits, borrower "
                    "multisign");
                env(set(lender, broker.brokerID, principalRequest),
                    kCounterparty(borrower),
                    Msig(sfCounterpartySignature, alice, bella),
                    Fee(env.current()->fees().base * 5));
            },
            [&, this](Env& env, BrokerInfo const& broker) {
                using namespace loan;
                msigSetup(env, borrower);
                Number const principalRequest = broker.asset(1'000).value();

                testcase(
                    "IOU authorized borrower, lender submits, borrower "
                    "multisign");
                env(set(lender, broker.brokerID, principalRequest),
                    kCounterparty(borrower),
                    Msig(sfCounterpartySignature, alice, bella),
                    Fee(env.current()->fees().base * 5));
            },
            CaseArgs{.requireAuth = true, .authorizeBorrower = true});

        testCase(
            [&, this](Env& env, BrokerInfo const& broker, auto&) {
                using namespace loan;
                Number const principalRequest = broker.asset(1'000).value();
                Vault const vault{env};
                auto tx = vault.set({.owner = lender, .id = broker.vaultID});
                tx[sfAssetsMaximum] = BrokerParameters::defaults().vaultDeposit;
                env(tx);
                env.close();

                testcase("Vault at maximum value");
                env(set(issuer, broker.brokerID, principalRequest),
                    kCounterparty(lender),
                    kInterestRate(TenthBips32(10'000)),
                    Sig(sfCounterpartySignature, lender),
                    Fee(env.current()->fees().base * 5),
                    Ter(tecLIMIT_EXCEEDED));
            },
            nullptr);

        testCase(
            [&, this](Env& env, BrokerInfo const& broker, auto&) {
                using namespace loan;
                Number const principalRequest = broker.asset(1'000).value();
                Vault const vault{env};
                auto tx = vault.set({.owner = lender, .id = broker.vaultID});
                tx[sfAssetsMaximum] =
                    BrokerParameters::defaults().vaultDeposit + broker.asset(1).number();
                env(tx);
                env.close();

                testcase("Vault maximum value exceeded");
                env(set(issuer, broker.brokerID, principalRequest),
                    kCounterparty(lender),
                    kInterestRate(TenthBips32(100'000)),
                    Sig(sfCounterpartySignature, lender),
                    Fee(env.current()->fees().base * 5),
                    kPaymentTotal(2),
                    kPaymentInterval(3600 * 24),
                    Ter(tecLIMIT_EXCEEDED));
            },
            nullptr);
    }

    // LoanSet in a closed-ended vault — phase gating and maturity bound.
    void
    testLoanSetClosedEnded()
    {
        testcase("LoanSet closed-ended: phase and maturity bound");
        using namespace jtx;
        using namespace loan;
        using d = NetClock::duration;
        using tp = NetClock::time_point;

        Account const issuer{"issuer"};
        Account const lender{"lender"};
        Account const borrower{"borrower"};

        // Common loan schedule used by the phase-rejection cases below.
        constexpr std::uint32_t kInterval = 3600u * 24u;  // 1 day
        constexpr std::uint32_t kTotal = 2u;

        // featureLendingProtocolV1_1 is excluded from `all_` by convention (see the comment on
        // `all_`), so callers must opt in. Closed-ended vaults are gated on this amendment; without
        // it VaultCreate returns temDISABLED and every follow-on txn sees tecNO_ENTRY.
        auto const withEnv = [&, this](auto&& body) {
            Env env(*this, testableAmendments() | featureLendingProtocolV1_1);
            env.fund(XRP(1'000'000'000), issuer, lender, borrower);
            env.close();
            PrettyAsset const asset{xrpIssue(), 1'000'000};
            body(env, asset);
        };

        auto const setLoan = [&](Env& env, BrokerInfo const& broker, TER expected) {
            env(set(lender, broker.brokerID, broker.asset(100).value()),
                kCounterparty(borrower),
                Sig(sfCounterpartySignature, borrower),
                Fee(env.current()->fees().base * 5),
                kPaymentTotal(kTotal),
                kPaymentInterval(kInterval),
                Ter(expected));
            env.close();
        };

        // 1. Rejected during Subscription: the broker is created in Subscription (skipPhaseAdvance
        // = true), then LoanSet is attempted before advancing past SubscriptionDate.
        withEnv([&](Env& env, PrettyAsset const& asset) {
            auto const broker = createVaultAndBroker(
                env,
                asset,
                lender,
                BrokerParameters{.vaultKind = VaultKind::ClosedEnded, .skipPhaseAdvance = true});
            setLoan(env, broker, tecTOO_SOON);
        });

        // 2. Rejected during Redemption: broker is set up normally (which lands the vault in
        // Investment), then advance the clock past RedemptionDate before attempting LoanSet.
        withEnv([&](Env& env, PrettyAsset const& asset) {
            auto const broker = createVaultAndBroker(
                env, asset, lender, BrokerParameters{.vaultKind = VaultKind::ClosedEnded});
            BEAST_EXPECT(broker.redemptionDate.has_value());
            using d = NetClock::duration;
            using tp = NetClock::time_point;
            env.close(tp{d{*broker.redemptionDate + 1}});
            setLoan(env, broker, tecEXPIRED);
        });

        // 3. Accepted during Investment when the schedule comfortably fits before RedemptionDate.
        withEnv([&](Env& env, PrettyAsset const& asset) {
            auto const broker = createVaultAndBroker(
                env, asset, lender, BrokerParameters{.vaultKind = VaultKind::ClosedEnded});
            setLoan(env, broker, tesSUCCESS);
        });

        // 4. Rejected during Investment when the loan's final payment would land fewer than
        // kLoanRedemptionBuffer seconds before RedemptionDate. Use a tight redemptionOffset and a
        // schedule whose final payment is well past that boundary.
        withEnv([&](Env& env, PrettyAsset const& asset) {
            constexpr std::uint32_t kRedemptionOffset = 3u * 24u * 3600u;
            auto const broker = createVaultAndBroker(
                env,
                asset,
                lender,
                BrokerParameters{
                    .vaultKind = VaultKind::ClosedEnded, .redemptionOffset = kRedemptionOffset});
            env(set(lender, broker.brokerID, broker.asset(100).value()),
                kCounterparty(borrower),
                Sig(sfCounterpartySignature, borrower),
                Fee(env.current()->fees().base * 5),
                kPaymentTotal(10u),
                kPaymentInterval(kInterval),
                Ter(tecNO_PERMISSION));
            env.close();
        });

        // 5. Boundary: a finalPayment exactly kLoanRedemptionBuffer seconds before
        // RedemptionDate is accepted; one second later is rejected. Uses payTotal = 1 so
        // finalPayment = startDate + interval.
        withEnv([&](Env& env, PrettyAsset const& asset) {
            auto const broker = createVaultAndBroker(
                env, asset, lender, BrokerParameters{.vaultKind = VaultKind::ClosedEnded});
            BEAST_EXPECT(broker.redemptionDate.has_value());

            auto const startDate = env.now().time_since_epoch().count();
            auto const acceptInterval = *broker.redemptionDate - kLoanRedemptionBuffer - startDate;
            env(set(lender, broker.brokerID, broker.asset(100).value()),
                kCounterparty(borrower),
                Sig(sfCounterpartySignature, borrower),
                Fee(env.current()->fees().base * 5),
                kPaymentTotal(1u),
                kPaymentInterval(acceptInterval),
                Ter(tesSUCCESS));
            env.close();

            auto const rejectInterval = *broker.redemptionDate - (kLoanRedemptionBuffer - 1) -
                env.now().time_since_epoch().count();
            env(set(lender, broker.brokerID, broker.asset(100).value()),
                kCounterparty(borrower),
                Sig(sfCounterpartySignature, borrower),
                Fee(env.current()->fees().base * 5),
                kPaymentTotal(1u),
                kPaymentInterval(rejectInterval),
                Ter(tecNO_PERMISSION));
            env.close();
        });

        // 6. A vault whose Investment window is exactly kMinInvestmentPeriod can originate a
        // minimum-interval, single-payment loan at the start of Investment, and rejects the same
        // schedule once StartDate no longer leaves kLoanRedemptionBuffer before RedemptionDate.
        // Do not pin an unrounded wall-clock instant: Env::close rounds to the close-time
        // resolution. Read env.now() (the same clock LoanSet::preclaim uses) and assert the
        // buffer relationship before each LoanSet.
        withEnv([&](Env& env, PrettyAsset const& asset) {
            auto const broker = createVaultAndBroker(
                env,
                asset,
                lender,
                BrokerParameters{
                    .vaultKind = VaultKind::ClosedEnded,
                    .subscriptionOffset = 300u,
                    .redemptionOffset = kMinInvestmentPeriod,
                    .skipPhaseAdvance = true});
            BEAST_EXPECT(broker.subscriptionDate.has_value());
            BEAST_EXPECT(broker.redemptionDate.has_value());

            auto const red = *broker.redemptionDate;
            auto const startDate = [&]() { return env.now().time_since_epoch().count(); };
            auto const minLoan = [&](TER expected) {
                env(set(lender, broker.brokerID, broker.asset(100).value()),
                    kCounterparty(borrower),
                    Sig(sfCounterpartySignature, borrower),
                    Fee(env.current()->fees().base * 5),
                    kPaymentTotal(1u),
                    kPaymentInterval(LoanSet::kMinPaymentInterval),
                    Ter(expected));
                env.close();
            };

            // First Investment ledger: the minimum schedule still clears the buffer.
            env.close(tp{d{*broker.subscriptionDate + 1}});
            BEAST_EXPECT(startDate() > *broker.subscriptionDate);
            BEAST_EXPECT(startDate() + LoanSet::kMinPaymentInterval + kLoanRedemptionBuffer <= red);
            minLoan(tesSUCCESS);

            // Still Investment, but the minimum schedule no longer clears the buffer.
            while (startDate() + LoanSet::kMinPaymentInterval + kLoanRedemptionBuffer <= red)
                env.close();
            BEAST_EXPECT(startDate() < red);
            minLoan(tecNO_PERMISSION);
        });
    }

    // LoanSet used to call canAddHolding unconditionally, so an existing
    // borrower line still failed with terNO_RIPPLE after the issuer cleared
    // DefaultRipple. After fixCleanup3_4_0, skip that gate when the holding
    // already exists.
    void
    testLoanSetExistingLineAfterIssuerClearsDefaultRipple()
    {
        using namespace jtx;
        using namespace loan;

        auto run = [this](FeatureBitset features, TER expected) {
            testcase(
                std::string(
                    "LoanSet existing borrower line after issuer "
                    "clears asfDefaultRipple (") +
                (features[fixCleanup3_4_0] ? "post" : "pre") + "-fixCleanup3_4_0)");

            Env env(*this, features);
            Account const issuer{"issuer"};
            Account const lender{"lender"};
            Account const borrower{"borrower"};

            env.fund(XRP(10'000), issuer, lender, borrower);
            env.close();
            env(fset(issuer, asfDefaultRipple));
            env.close();

            PrettyAsset const usd{issuer["USD"]};
            env(trust(lender, usd(10'000'000)));
            env(trust(borrower, usd(10'000'000)));
            env.close();
            env(pay(issuer, lender, usd(2'000'000)));
            env(pay(issuer, borrower, usd(1'000)));
            env.close();
            BEAST_EXPECT(env.le(keylet::trustLine(borrower.id(), usd.raw().get<Issue>())));

            auto const broker = createVaultAndBroker(env, usd, lender);

            env(fclear(issuer, asfDefaultRipple));
            env.close();

            Number const destBefore = env.balance(borrower, usd.raw()).number();
            env(set(borrower, broker.brokerID, usd(100).value()),
                Sig(sfCounterpartySignature, lender),
                Fee(env.current()->fees().base * 2),
                Ter(expected));
            env.close();

            Number const destAfter = env.balance(borrower, usd.raw()).number();
            if (isTesSuccess(expected))
            {
                BEAST_EXPECT(destAfter == destBefore + Number{100});
            }
            else
            {
                BEAST_EXPECT(destAfter == destBefore);
            }
        };

        run(all_ - fixCleanup3_4_0, terNO_RIPPLE);
        run(all_, tesSUCCESS);
    }

    void
    testPistisLending()
    {
        using namespace jtx;
        using namespace loan;
        Account const lender{"pistis-lender"};
        Account const borrower{"pistis-borrower"};
        Account const issuer{"pistis-issuer"};
        Account const secondIssuer{"pistis-second-issuer"};
        std::string const type = "PISTIS:CREDIT-ELIG:B:v1";

        // 0 absent; 1 unaccepted; 2 expired; 3 revoked after signing;
        // 4 wrong issuer; 5 wrong type; 6 depositor only; 7 valid;
        // 8 multiple valid alternatives; 9 deleted domain;
        // 10 missing VaultID; 11 wrong VaultID; 12 expired + valid alternative;
        // 13 zero VaultID; 14 reuse in a second Vault.
        for (bool const brokerSubmits : {false, true})
        {
            for (unsigned scenario = 0; scenario != 15; ++scenario)
            {
                testcase(
                    "Pistis credentials " + std::to_string(scenario) +
                    (brokerSubmits ? " broker submits" : " borrower submits"));
                Env env(*this, all_ | featurePistisLending);
                env.fund(XRP(10'000'000), lender, borrower, issuer, secondIssuer);
                env.close();
                pdomain::Credentials const accepted{{issuer, type}, {secondIssuer, type}};
                env(pdomain::setTx(issuer.id(), accepted));
                auto const domainID = pdomain::getNewDomain(env.meta());
                env.close();
                env(pdomain::setTx(issuer.id(), {{issuer, "PISTIS:DEPOSITOR:v1"}}));
                auto const depositorDomainID = pdomain::getNewDomain(env.meta());
                env.close();
                BrokerParameters params;
                params.vaultFlags = tfVaultPrivate;
                params.depositorDomainID = depositorDomainID;
                params.borrowerDomainID = domainID;
                PrettyAsset const asset{xrpIssue(), 1'000'000};
                auto const broker = createVaultAndBroker(env, asset, lender, params);
                auto const vault = env.le(keylet::vault(broker.vaultID));
                if (!BEAST_EXPECT(vault))
                    continue;
                BEAST_EXPECT(vault->at(sfBorrowerDomainID) == domainID);

                if (scenario != 0)
                {
                    auto const& credentialIssuer = scenario == 4 ? lender : issuer;
                    auto const credentialType = scenario == 5 ? "OTHER"
                        : scenario == 6                       ? "PISTIS:DEPOSITOR:v1"
                                                              : type;
                    auto credential =
                        credentials::create(borrower, credentialIssuer, credentialType);
                    if (scenario == 2 || scenario == 12)
                        credential[sfExpiration] = env.now().time_since_epoch().count() + 30;
                    env(credential);
                    if (scenario != 1)
                        env(credentials::accept(borrower, credentialIssuer, credentialType));
                    env.close();
                    if (scenario == 2 || scenario == 12)
                    {
                        env.close(env.now() + std::chrono::seconds{60});
                        env.close();
                    }
                }
                if (scenario == 6)
                {
                    // This credential really authorizes deposits to the same Vault,
                    // but must not authorize borrowing from it.
                    env(Vault::deposit(
                        {.depositor = borrower, .id = broker.vaultID, .amount = asset(100)}));
                    env.close();
                }
                if (scenario == 8 || scenario == 12)
                {
                    env(credentials::create(borrower, secondIssuer, type));
                    env(credentials::accept(borrower, secondIssuer, type));
                    env.close();
                }
                if (scenario == 9)
                {
                    env(pdomain::deleteTx(issuer.id(), domainID));
                    env.close();
                }

                TER const expected = scenario == 13 ? TER{temMALFORMED}
                    : scenario == 2                 ? TER{tecEXPIRED}
                    : scenario == 9                 ? TER{tecOBJECT_NOT_FOUND}
                    : (scenario == 7 || scenario == 8 || scenario == 12 || scenario == 14)
                    ? TER{tesSUCCESS}
                    : TER{tecNO_PERMISSION};
                auto tx =
                    set(brokerSubmits ? lender : borrower, broker.brokerID, asset(100).value());
                if (scenario != 10)
                    tx[sfVaultID] = to_string(
                        scenario == 13       ? uint256{}
                            : scenario == 11 ? uint256{1}
                                             : broker.vaultID);
                if (brokerSubmits)
                    tx[sfCounterparty] = borrower.human();
                auto signedTx = env.jt(
                    tx,
                    Sig(sfCounterpartySignature, brokerSubmits ? borrower : lender),
                    Fee(env.current()->fees().base * 5),
                    Ter(expected));
                if (scenario == 3)
                {
                    // The fully signed transaction must not retain old authorization.
                    env(credentials::deleteCred(issuer, borrower, issuer, type));
                    env.close();
                }
                auto const loanKey = keylet::loan(
                    broker.brokerID,
                    SeqProxy::rawSequence(env.le(broker.brokerKeylet())->at(sfLoanSequence)));
                auto const beforeAvailable =
                    env.le(keylet::vault(broker.vaultID))->at(sfAssetsAvailable);
                env.submit(signedTx);
                env.close();
                auto const created = env.le(loanKey);
                if (isTesSuccess(expected))
                {
                    if (!BEAST_EXPECT(created))
                        continue;
                    auto const pairs = env.le(keylet::permissionedDomain(domainID))
                                           ->getFieldArray(sfAcceptedCredentials);
                    auto const expectedIssuer = scenario == 12 ? secondIssuer.id()
                        : scenario == 8                        ? pairs[0].getAccountID(sfIssuer)
                                                               : issuer.id();
                    BEAST_EXPECT(created->at(sfCredentialIssuer) == expectedIssuer);
                    BEAST_EXPECT(
                        created->getFieldVL(sfCredentialType) ==
                        std::vector<std::uint8_t>(type.begin(), type.end()));

                    auto update = Vault::set({.owner = lender, .id = broker.vaultID});
                    update[sfBorrowerDomainID] = to_string(uint256{});
                    env(update, Ter(tecHAS_OBLIGATIONS));

                    if (scenario == 14)
                    {
                        auto const second = createVaultAndBroker(env, asset, lender, params);
                        auto const secondLoan = keylet::loan(
                            second.brokerID,
                            SeqProxy::rawSequence(env.le(second.brokerKeylet())->at(sfLoanSequence)));
                        auto secondTx = set(borrower, second.brokerID, asset(100).value());
                        secondTx[sfVaultID] = to_string(second.vaultID);
                        env(secondTx,
                            Sig(sfCounterpartySignature, lender),
                            Fee(env.current()->fees().base * 5));
                        env.close();
                        auto const secondCreated = env.le(secondLoan);
                        if (BEAST_EXPECT(secondCreated))
                            BEAST_EXPECT(secondCreated->at(sfCredentialIssuer) == issuer.id());
                    }

                    // Revocation is not an authorization requirement for repayment.
                    env(credentials::deleteCred(issuer, borrower, issuer, type));
                    env.close();
                    // The only scheduled installment requires a regular payment.
                    env(loan::pay(borrower, loanKey.key, asset(100)));
                    env.close();
                    BEAST_EXPECT(env.le(loanKey)->at(sfCredentialIssuer) == expectedIssuer);
                    auto const available = env.le(broker.vaultKeylet())->at(sfAssetsAvailable);
                    env(Vault::withdraw(
                        {.depositor = lender,
                         .id = broker.vaultID,
                         .amount = STAmount{asset.raw(), available}}));
                    env.close();
                    // Even with no shares, the paid Loan is still present.
                    env(update, Ter(tecHAS_OBLIGATIONS));
                    env(loan::del(borrower, loanKey.key));
                    env.close();
                    BEAST_EXPECT(!env.le(loanKey));
                    env(update);
                    env.close();
                    BEAST_EXPECT(!env.le(broker.vaultKeylet())->isFieldPresent(sfBorrowerDomainID));
                }
                else
                {
                    BEAST_EXPECT(!created);
                    BEAST_EXPECT(
                        env.le(keylet::vault(broker.vaultID))->at(sfAssetsAvailable) ==
                        beforeAvailable);
                }
            }
        }
    }

    void
    testPistisLegacyLoans()
    {
        using namespace jtx;
        Account const lender{"pistis-legacy-lender"};
        Account const borrower{"pistis-legacy-borrower"};
        for (bool const enabled : {false, true})
        {
            testcase(enabled ? "Pistis legacy loans enabled" : "Pistis legacy loans disabled");
            auto features = all_;
            if (!enabled)
                features = features - featurePistisLending;
            Env env(*this, features);
            env.fund(XRP(10'000'000), lender, borrower);
            env.close();
            PrettyAsset const asset{xrpIssue(), 1'000'000};
            auto const broker = createVaultAndBroker(env, asset, lender);
            auto submit = [&](bool withVaultID, TER expected) {
                auto const sequence = env.le(broker.brokerKeylet())->at(sfLoanSequence);
                auto tx = loan::set(borrower, broker.brokerID, asset(100).value());
                if (withVaultID)
                    tx[sfVaultID] = to_string(broker.vaultID);
                env(tx,
                    Sig(sfCounterpartySignature, lender),
                    Fee(env.current()->fees().base * 5),
                    Ter(expected));
                env.close();
                if (isTesSuccess(expected))
                {
                    auto const loan =
                        env.le(keylet::loan(broker.brokerID, SeqProxy::rawSequence(sequence)));
                    if (BEAST_EXPECT(loan))
                    {
                        BEAST_EXPECT(!loan->isFieldPresent(sfCredentialIssuer));
                        BEAST_EXPECT(!loan->isFieldPresent(sfCredentialType));
                    }
                }
            };
            submit(false, tesSUCCESS);
            submit(true, enabled ? TER{tesSUCCESS} : TER{temDISABLED});
        }
    }

    void
    testPistisVaultConfiguration()
    {
        using namespace jtx;
        Account const owner{"pistis-owner"};
        Account const issuer{"pistis-issuer"};
        for (bool const enabled : {false, true})
        {
            testcase(enabled ? "Pistis vault enabled" : "Pistis vault disabled");
            auto features = all_;
            if (!enabled)
                features = features - featurePistisLending;
            Env env(*this, features);
            env.fund(XRP(10'000'000), owner, issuer);
            env.close();
            env(pdomain::setTx(issuer.id(), {{issuer, "PISTIS:CREDIT-ELIG:B:v1"}}));
            auto const domainID = pdomain::getNewDomain(env.meta());
            env.close();
            Vault const vault{env};
            auto [tx, vaultKey] = vault.create({.owner = owner, .asset = xrpIssue()});
            tx[sfBorrowerDomainID] = to_string(uint256{});
            env(tx, Ter(enabled ? TER{temMALFORMED} : TER{temDISABLED}));
            tx[sfBorrowerDomainID] = to_string(uint256{1});
            env(tx, Ter(enabled ? TER{tecOBJECT_NOT_FOUND} : TER{temDISABLED}));
            // Failed tec transactions consume Sequence: rebuild the expected keylet.
            std::tie(tx, vaultKey) = vault.create({.owner = owner, .asset = xrpIssue()});
            tx[sfBorrowerDomainID] = to_string(domainID);
            if (!enabled)
            {
                env(tx, Ter(temDISABLED));
                tx.removeMember(sfBorrowerDomainID.jsonName);
            }
            env(tx);
            env.close();
            auto update = Vault::set({.owner = owner, .id = vaultKey.key});
            update[sfBorrowerDomainID] = to_string(domainID);
            env(update, Ter(enabled ? TER{tesSUCCESS} : TER{temDISABLED}));
            if (!enabled)
                continue;
            update[sfBorrowerDomainID] = to_string(uint256{});
            env(update);
            env.close();
            BEAST_EXPECT(!env.le(vaultKey)->isFieldPresent(sfBorrowerDomainID));
            update[sfBorrowerDomainID] = to_string(domainID);
            env(update);
            env.close();
            env(vault.deposit({.depositor = owner, .id = vaultKey.key, .amount = XRP(100)}));
            env.close();
            // Same reference is a no-op; removal and replacement are changes.
            env(update);
            update[sfBorrowerDomainID] = to_string(uint256{});
            env(update, Ter(tecHAS_OBLIGATIONS));
            env.close();
        }
    }

public:
    void
    run() override
    {
        testPistisVaultConfiguration();
        testPistisLending();
        testPistisLegacyLoans();
        for (auto const& features : jtx::amendmentCombinations(
                 {fixCleanup3_1_3, fixCleanup3_2_0, featureMPTokensV2}, all_))
            testLoanSet(features);

        testLoanSetClosedEnded();
        testLoanSetExistingLineAfterIssuerClearsDefaultRipple();
    }
};

BEAST_DEFINE_TESTSUITE(LoanSet, tx, xrpl);

}  // namespace xrpl::test
