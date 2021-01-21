#pragma once

/// <summary>
/// Useful global constants
/// </summary>
namespace ff::constants
{
    /// <summary>
    /// Fixed amount of times per second that the game loop advances
    /// </summary>
    const double advances_per_second = 60.0;
    const size_t advances_per_second_s = 60;

    /// <summary>
    /// Each time the game advances, this fixed amount of time has passed
    /// </summary>
    const double seconds_per_advance = 1.0 / 60.0;

    const double pi = 3.1415926535897932384626433832795; ///< PI
    const double pi2 = 6.283185307179586476925286766559; ///< 2 * PI
}
