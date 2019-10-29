
var config = {
    devices_src: [
        { name: 'Laser-A', port:'1', icon:'icons/default_laser.png'},
        { name: 'Laser-B', port:'10', icon:'icons/default_laser.png'},
        { name: 'Laser-C', port:'19', icon:'icons/default_laser.png'},
        { name: 'Laser-D', port:'28', icon:'icons/default_laser.png'},
    ],

    devices_dest: [
        { name: 'Detector-A', port:'44', icon:'icons/default_detector.png'},
        { name: 'Detector-B', port:'56', icon:'icons/default_detector.png'},
        { name: 'Detector-C', port:'57', icon:'icons/default_detector.png'},
        { name: 'Detector-D', port:'64', icon:'icons/default_detector.png'},
        { name: 'Detector-E', port:'65', icon:'icons/default_detector.png'},
        { name: 'Detector-F', port:'66', icon:'icons/default_detector.png'},
    ],

    clients: [
        {
            name:    '4-Qubit Cluster Maker',
            owner:   'Jeremy',
            location:'Bench 5',
            inputs:[
                {name: 'IN', port:'42', icon:'icons/in_to_pair_source.png'},
            ],
            outputs:[
                {name: 'As/Bs OUT', port:'24', icon:'icons/out_from_waveguide.png'},
                {name: 'Cs/Ds OUT', port:'25', icon:'icons/out_from_waveguide.png'},
                {name: 'Ai/Bi OUT', port:'33', icon:'icons/out_from_waveguide.png'},
                {name: 'Ci/Di OUT', port:'34', icon:'icons/out_from_waveguide.png'},
            ],
        },
        {
            name:    'QKD Setup',
            owner:   'Alasdair',
            location:'Bench 8',
            inputs:[
                {name: 'Bob IN', port:'45', icon:'icons/qkd_bob_input.png'},
            ],
            outputs:[
                {name: 'Card Alice', port:'7', icon:'icons/card_alice_output.png'},
                {name: 'Bob OUT', port:'8', icon:'icons/qkd_bob_output.png'},
            ],
        }
    ],
};

