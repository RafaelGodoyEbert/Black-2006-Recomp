/* Only the final secondary-requirement decisions use this override. Native
 * primary-objective checks, statistics and collected-objective data stay intact.
 * Read the live setting so a pause-menu change applies to the current mission.
 */
int black_secondary_requirement_met(int native_met)
{
    PcVideoSettings settings;
    if (native_met) return 1;
    pc_video_get(&settings);
    return settings.optional_secondaries != 0;
}
