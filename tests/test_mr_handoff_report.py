import copy
import json
from pathlib import Path
import sys
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
import analyze_mr_handoff as profile

BOOT = '11111111222233334444555555555555'
OTHER_BOOT = 'aaaaaaaa222233334444555555555555'
PREFIX = '[Steam Link Native Passthrough] '


def lines(records):
    return '\n'.join(json.dumps(r) for r in records)


def marker(phase, event, stamp, **changes):
    return dict(schema=profile.MARKER_SCHEMA, phase=phase, event=event,
                monotonic_us=stamp, boot_id=BOOT, game='GloomhavenVR',
                mode_confirmed_by_operator=True, **changes)


def boundaries():
    return [marker('mr-on', 'start', 100), marker('mr-on', 'end', 200),
            marker('mr-off', 'start', 300), marker('mr-off', 'end', 400)]


def row(stamp, message, pid=123, boot=BOOT, stream='abc'):
    return {'__MONOTONIC_TIMESTAMP': str(stamp), '_BOOT_ID': boot,
            '_PID': str(pid), '_STREAM_ID': stream, 'MESSAGE': PREFIX + message}


def duration(stamp, samples, total, role='compositor', **identity):
    return row(stamp, f'handoff duration {role}: samples={samples}, total-us={total}, '
               f'approximate-mean-us={total // max(1, samples)}; lifetime CPU callback statistics', **identity)


class HandoffReportTest(unittest.TestCase):
    def report(self, records, markers=None):
        return profile.analyze(lines(records), lines(boundaries() if markers is None else markers))

    def group(self, result, phase='mr-on', family='handoff', index=0):
        return [g for g in result['phases'][phase]['groups'] if g['family'] == family][index]

    def test_phase_deltas_exclude_warmup_and_other_device_boots(self):
        result = self.report([duration(90, 900, 90000000), duration(110, 1000, 90100000),
                              duration(190, 1100, 90600000), duration(250, 1150, 91000000),
                              duration(310, 1200, 92000000), duration(390, 1400, 92200000),
                              duration(150, 9999, 99999999, boot=OTHER_BOOT)])
        on = self.group(result)['epochs'][0]
        off = self.group(result, 'mr-off')['epochs'][0]
        self.assertEqual(on['metrics']['callbacks'], 100)
        self.assertEqual(on['metrics']['approximate_mean_callback_ms'], 5)
        self.assertEqual(off['metrics']['approximate_mean_callback_ms'], 1)
        self.assertEqual((on['first_report_us'], on['last_report_us']), (110, 190))
        self.assertTrue(result['both_operator_windows_present'])
        self.assertIsNone(result['motion_to_photon_ms'])
        self.assertFalse(result['exact_stream_pose_proven'] or result['causal_diagnosis_proven'])

    def test_processes_streams_and_roles_remain_independent(self):
        rows = []
        for identity in ({}, {'pid': 456}, {'stream': 'new'}, {'role': 'video'}):
            rows.extend([duration(110, 1, 100, **identity), duration(190, 11, 20100, **identity)])
        groups = self.report(rows)['phases']['mr-on']['groups']
        self.assertEqual(len(groups), 4)
        self.assertTrue(all(g['epochs'][0]['metrics']['approximate_mean_callback_ms'] == 2 for g in groups))

    def test_reset_cannot_create_negative_or_cross_epoch_delta(self):
        result = self.report([duration(110, 100, 900000), duration(120, 110, 920000),
                              duration(160, 0, 0), duration(190, 10, 10000)])
        group = self.group(result)
        self.assertEqual(group['counter_resets_observed'], 1)
        self.assertEqual([e['metrics']['approximate_mean_callback_ms'] for e in group['epochs']], [2, 1])

    def test_threshold_maximum_is_explicitly_lifetime(self):
        result = self.report([row(110, 'paired handoff latency video: max-us=90000; over-11ms/22ms/50ms=100/20/4'),
                              row(190, 'paired handoff latency video: max-us=90000; over-11ms/22ms/50ms=104/21/4')])
        metrics = self.group(result, family='handoff_thresholds')['epochs'][0]['metrics']
        self.assertEqual(metrics, dict(callbacks_over_11ms=4, callbacks_over_22ms=1,
                                       callbacks_over_50ms=0, lifetime_max_ms_at_end=90))

    def test_separate_decode_queue_collect_and_publication_deltas(self):
        def stages(stamp, values):
            return row(stamp, ('HEVC mask stages compositor: started/collected=%d/%d; '
                'queue total/max-us=%d/%d; completed-to-collect total/max-us=%d/%d; '
                'publication samples/total/max-us=%d/%d/%d; R8-buffer grows/reuses=%d/%d') % values)
        result = self.report([stages(110, (100, 99, 100000, 10000, 99000, 9000, 98, 98000, 8000, 2, 96)),
                              stages(190, (110, 109, 120000, 10000, 129000, 9000, 108, 103000, 8000, 2, 106))])
        metrics = self.group(result, family='mask_stages')['epochs'][0]['metrics']
        self.assertEqual(metrics['approximate_queue_mean_ms'], 2)
        self.assertEqual(metrics['approximate_completed_to_collect_mean_ms'], 3)
        self.assertEqual(metrics['approximate_publication_mean_ms'], .5)
        self.assertEqual(metrics['buffer_grows'], 0)
        self.assertEqual(metrics['buffer_reuses'], 10)
        self.assertNotIn('total_pipeline_ms', metrics)

    def test_worker_rounded_lifetime_mean_is_not_differenced(self):
        def worker(stamp, submitted, attempts, mean):
            return row(stamp, f'HEVC mask pipeline compositor: workers=2, slots=8; '
                f'submitted/backpressure/stale={submitted}/1/0; contexts=2; attempts={attempts}; '
                f'group-affine IPPP8; worker mean/max-us={mean}/9000; temporal-decoded/missing-reference=3/0')
        result = self.report([worker(110, 100000, 100000, 501), worker(190, 100001, 100001, 500)])
        metrics = self.group(result, family='mask_worker')['epochs'][0]['metrics']
        self.assertEqual(metrics['attempts'], 1)
        self.assertEqual(metrics['worker_lifetime_approximate_mean_ms_at_end'], .5)
        self.assertNotIn('worker_interval_mean_ms', metrics)
        bounds = metrics['worker_interval_mean_bounds']
        self.assertEqual(bounds['lower_ms'], 0)
        self.assertEqual(bounds['upper_ms'], .5)

    def test_worker_interval_bounds_enclose_actual_duration_without_false_precision(self):
        first, last = {'attempts': 100, 'lifetime_mean_us': 1000}, {'attempts': 110, 'lifetime_mean_us': 1100}
        bounds = profile.worker_mean_bounds(first, last)
        for start_total in (100000, 100099):
            for end_total in (121000, 121109):
                actual = (end_total - start_total) / 10 / 1000
                self.assertLessEqual(bounds['lower_ms'], actual)
                self.assertGreaterEqual(bounds['upper_ms'], actual)
        self.assertIsNone(profile.worker_mean_bounds(first, first))
        self.assertEqual(profile.worker_mean_bounds({'attempts': 0, 'lifetime_mean_us': 0}, first)['lower_ms'], 1)
        with self.assertRaises(ValueError):
            profile.worker_mean_bounds(first, {'attempts': 101, 'lifetime_mean_us': 1})

    def test_relay_pid_is_not_presented_as_native_process_identity(self):
        records = [duration(110, 1, 100), duration(190, 2, 200)]
        for record in records:
            record.update(_COMM='python3', _SYSTEMD_USER_UNIT='steamvr-logs.service')
        result = self.report(records)
        group = self.group(result)
        self.assertEqual(group['journal_source_kind'], 'log-relay')
        self.assertEqual(group['journal_comm'], 'python3')
        self.assertIn('forwarding times', result['scope'])

    def test_missing_debug_or_single_snapshot_is_unknown(self):
        report = self.report([{'MESSAGE': 'ordinary non-debug message'}])
        self.assertEqual(report['phases']['mr-on']['status'], 'no-counter-snapshots')
        self.assertEqual(report['ignored_journal_records'], 1)
        result = self.report([duration(110, 100, 300)])
        epoch = self.group(result)['epochs'][0]
        self.assertEqual(epoch['status'], 'insufficient-snapshots')
        self.assertIsNone(epoch['metrics'])

    def test_no_new_samples_is_not_zero_latency(self):
        result = self.report([duration(110, 100, 300), duration(190, 100, 300)])
        self.assertIsNone(self.group(result)['epochs'][0]['metrics']['approximate_mean_callback_ms'])

    def test_exact_duplicates_deduplicate_but_conflicts_refuse(self):
        a, b = duration(110, 1, 100), duration(190, 2, 200)
        self.assertEqual(self.group(self.report([b, a, a]))['epochs'][0]['reports'], 2)
        with self.assertRaises(ValueError):
            self.report([a, duration(110, 2, 100)])

    def test_required_journal_identity_and_bounded_unsigned_values(self):
        good = duration(110, 1, 100)
        for key, value in (('_PID', None), ('_PID', '0'), ('_PID', True),
                           ('_BOOT_ID', ''), ('__MONOTONIC_TIMESTAMP', '-1'),
                           ('_STREAM_ID', ['a', 'b'])):
            bad = {**good, key: value}
            with self.subTest(key=key, value=value), self.assertRaises(ValueError):
                self.report([bad])
        with self.assertRaises(ValueError):
            self.report([duration(110, 2**64, 1)])
        with self.assertRaises(ValueError):
            self.report([duration(110, 1, 10**21)])

    def test_operator_boundaries_require_confirmed_same_game_boot_and_no_overlap(self):
        for index, key, value in ((0, 'mode_confirmed_by_operator', False),
                                  (1, 'game', 'another'), (2, 'game', 'another'),
                                  (2, 'boot_id', OTHER_BOOT), (1, 'monotonic_us', 100),
                                  (2, 'monotonic_us', 199)):
            records = copy.deepcopy(boundaries())
            records[index][key] = value
            with self.subTest(key=key, value=value), self.assertRaises(ValueError):
                self.report([], records)
        for records in (boundaries()[:-1], boundaries() + [boundaries()[0]]):
            with self.assertRaises(ValueError):
                self.report([], records)

    def test_uuid_formats_normalize_and_single_complete_phase_is_labeled(self):
        records = boundaries()[:2]
        records[0]['boot_id'] = '11111111-2222-3333-4444-555555555555'
        result = self.report([duration(110, 1, 100), duration(190, 2, 200)], records)
        self.assertEqual(result['phases']['mr-on']['boot_id'], BOOT)
        self.assertFalse(result['both_operator_windows_present'])

    def test_json_duplicates_nonfinite_and_budgets_refuse(self):
        with self.assertRaises(ValueError):
            profile.decode('{"a": 1, "a": 2}')
        with self.assertRaises(ValueError):
            profile.decode('{"a": NaN}')
        with patch.object(profile, 'MAX_BYTES', 10), self.assertRaises(ValueError):
            profile.bounded_lines('x' * 11)
        with patch.object(profile, 'MAX_LINES', 2), self.assertRaises(ValueError):
            profile.bounded_lines('x\nx\nx')


if __name__ == '__main__':
    unittest.main()
