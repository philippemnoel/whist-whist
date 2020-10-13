#ifndef WEBSERVER_H
#define WEBSERVER_H

/**
 * @brief                          Queries the webserver for various parameters
 */
void update_webserver_parameters();

/**
 * @brief                          Queries the webserver to ask if a VM is
 *                                 development VM
 *
 * @returns                        True if a VM is a "development" VM (dev
 *                                 protocol branch), False otherwise
 */
bool is_dev_vm();

/**
 * @brief                          Queries the webserver to get the VM's aes
 * private key
 *
 * @returns                        The VM's 16-byte aes private key
 */
char* get_private_key();

/**
 * @brief                          Queries the webserver to get the using_stun
 * status
 *
 * @returns                        The using_stun status
 */
bool get_using_stun();

/**
 * @brief                          Queries the webserver for the get access token
 * status
 *
 * @returns                        The access token
 */
char* get_access_token();

/**
 * @brief                          Retrieves the protocol branch this program is
 *                                 running by asking the webserver
 *
 * @returns                        The string of the branch name
 */
char* get_branch();

#endif
