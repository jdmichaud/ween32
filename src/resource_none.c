/* The empty resource file a program links when it has none.
 *
 * In its own translation unit on purpose: a weak definition sitting beside
 * the code that reads it can be bound there and then, and a program that
 * embeds a real .res would find the empty one still being read. Kept apart,
 * the reference is the linker's to resolve and the strong definition wins.
 */

__attribute__((weak)) const unsigned char ween_app_resource_data[1] = { 0 };
__attribute__((weak)) const unsigned int ween_app_resource_len = 0;
